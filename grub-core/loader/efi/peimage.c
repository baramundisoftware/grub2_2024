/* peimage.c - load EFI PE binaries (for Secure Boot support) */

// SPDX-License-Identifier: GPL-3.0+

#include <grub/cache.h>
#include <grub/cpu/efi/memory.h>
#include <grub/dl.h>
#include <grub/efi/efi.h>
#include <grub/efi/pe32.h>
#include <grub/efi/peimage.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/setjmp.h>

GRUB_MOD_LICENSE ("GPLv3+");


static grub_dl_t my_mod;

struct image_info
{
  grub_efi_loaded_image_t loaded_image;

  grub_efi_device_path_t *orig_file_path;

  void *data;
  grub_efi_uint32_t data_size;
  grub_efi_uint16_t machine;
  grub_efi_uint16_t num_sections;
  struct grub_pe32_section_table *section;
  struct grub_pe32_data_directory *reloc;
  grub_uint64_t image_base;
  grub_uint32_t section_alignment;
  grub_uint32_t header_size;
  void *alloc_addr;
  grub_uint32_t alloc_pages;

  grub_efi_status_t (__grub_efi_api *entry_point) (
      grub_efi_handle_t image_handle, grub_efi_system_table_t *system_table);

  grub_jmp_buf jmp;
  grub_efi_status_t exit_status;
};

static grub_uint16_t machines[] = {
#if defined(__x86_64__)
  GRUB_PE32_MACHINE_X86_64,
#elif defined(__i386__)
  GRUB_PE32_MACHINE_I386,
#elif defined(__aarch64__)
  GRUB_PE32_MACHINE_ARM64,
#elif defined(__arm__)
  GRUB_PE32_MACHINE_ARMTHUMB_MIXED,
#elif defined(__riscv) && __riscv_xlen == 32
  GRUB_PE32_MACHINE_RISCV32,
#elif defined(__riscv) && __riscv_xlen == 64
  GRUB_PE32_MACHINE_RISCV64,
#endif
};

// Forward declare unload_image handler
static grub_efi_status_t __grub_efi_api
do_unload_image (grub_efi_handle_t image_handle);

// Original exit handler
static grub_efi_status_t (__grub_efi_api *exit_orig) (
    grub_efi_handle_t image_handle, grub_efi_status_t exit_status,
    grub_efi_uintn_t exit_data_size, grub_efi_char16_t *exit_data);

// Original unload_image handler
static grub_efi_status_t (__grub_efi_api *unload_image_orig) (
  grub_efi_handle_t image_handle);

/**
 * check_machine_type() - check if the machine type matches the architecture
 *
 * @machine:	the value of the Machine field of the COFF file header.
 * Return:	status code
 */
static grub_efi_status_t
check_machine_type (grub_uint16_t machine)
{
  for (grub_size_t i = 0; i < sizeof (machines) / sizeof (*machines); ++i)
    {
      if (machine == machines[i])
	return GRUB_EFI_SUCCESS;
    }

  return GRUB_EFI_LOAD_ERROR;
}

/**
 * check_pe_header() - check the headers of a PE-COFF image
 *
 * @info:	information about the image
 */
static grub_efi_status_t
check_pe_header (struct image_info *info)
{
  struct grub_msdos_image_header *dos_stub = info->data;
  void *pe_magic;
  struct grub_pe32_coff_header *coff_header;
  struct grub_pe32_optional_header *pe32_header;
  struct grub_pe64_optional_header *pe64_header;

  if (info->data_size < sizeof (struct grub_msdos_image_header))
    {
      grub_error (GRUB_ERR_BAD_OS, "truncated image");
      return GRUB_EFI_LOAD_ERROR;
    }
  if (dos_stub->msdos_magic != GRUB_PE32_MAGIC)
    {
      grub_error (GRUB_ERR_BAD_OS, "not a PE-COFF file");
      return GRUB_EFI_UNSUPPORTED;
    }
  if (info->data_size < dos_stub->pe_image_header_offset
			    + GRUB_PE32_SIGNATURE_SIZE
			    + sizeof (struct grub_pe32_coff_header)
			    + sizeof (struct grub_pe64_optional_header))
    {
      grub_error (GRUB_ERR_BAD_OS, "truncated image");
      return GRUB_EFI_LOAD_ERROR;
    }
  pe_magic
      = (void *)((unsigned long)info->data + dos_stub->pe_image_header_offset);
  if (grub_memcmp (pe_magic, GRUB_PE32_SIGNATURE, GRUB_PE32_SIGNATURE_SIZE))
    {
      grub_error (GRUB_ERR_BAD_OS, "not a PE-COFF file");
      return GRUB_EFI_LOAD_ERROR;
    }

  coff_header = (void *)((unsigned long)pe_magic + GRUB_PE32_SIGNATURE_SIZE);
  info->machine = coff_header->machine;
  info->num_sections = coff_header->num_sections;

  if (check_machine_type (info->machine) != GRUB_EFI_SUCCESS)
    {
      grub_error (GRUB_ERR_BAD_OS, "wrong machine type %u",
		  coff_header->machine);
      return GRUB_EFI_LOAD_ERROR;
    }

  pe32_header = (void *)((unsigned long)coff_header + sizeof (*coff_header));
  pe64_header = (void *)((unsigned long)coff_header + sizeof (*coff_header));

  switch (pe32_header->magic)
    {
    case GRUB_PE32_PE32_MAGIC:
      if (pe32_header->subsystem != GRUB_PE32_SUBSYSTEM_EFI_APPLICATION)
	{
	  grub_error (GRUB_ERR_BAD_OS, "expected EFI application");
	  return GRUB_EFI_LOAD_ERROR;
	}
      info->section_alignment = pe32_header->section_alignment;
      info->image_base = pe32_header->image_base;
      info->loaded_image.image_size = pe32_header->image_size;
      info->entry_point = (void *)(unsigned long)pe32_header->entry_addr;
      info->header_size = pe32_header->header_size;
      if (info->data_size < info->header_size)
	{
	  grub_error (GRUB_ERR_BAD_OS, "truncated image");
	  return GRUB_EFI_LOAD_ERROR;
	}

      if (pe32_header->num_data_directories >= 6
	  && pe32_header->base_relocation_table.size)
	info->reloc = &pe32_header->base_relocation_table;

      info->section
	  = (void *)((unsigned long)&pe32_header->export_table
		     + pe32_header->num_data_directories
			   * sizeof (struct grub_pe32_data_directory));
      break;
    case GRUB_PE32_PE64_MAGIC:
      if (pe64_header->subsystem != GRUB_PE32_SUBSYSTEM_EFI_APPLICATION)
	{
	  grub_error (GRUB_ERR_BAD_OS, "expected EFI application");
	  return GRUB_EFI_LOAD_ERROR;
	}
      info->section_alignment = pe64_header->section_alignment;
      info->image_base = pe64_header->image_base;
      info->loaded_image.image_size = pe64_header->image_size;
      info->entry_point = (void *)(unsigned long)pe64_header->entry_addr;
      info->header_size = pe64_header->header_size;
      if (info->data_size < info->header_size)
	{
	  grub_error (GRUB_ERR_BAD_OS, "truncated image");
	  return GRUB_EFI_LOAD_ERROR;
	}

      if (pe64_header->num_data_directories >= 6
	  && pe64_header->base_relocation_table.size)
	info->reloc = &pe64_header->base_relocation_table;

      info->section
	  = (void *)((unsigned long)&pe64_header->export_table
		     + pe64_header->num_data_directories
			   * sizeof (struct grub_pe32_data_directory));
      break;
    default:
      grub_error (GRUB_ERR_BAD_OS, "not a PE-COFF file");
      return GRUB_EFI_LOAD_ERROR;
    }

  if ((unsigned long)info->section
	  + info->num_sections * sizeof (*info->section)
      > (unsigned long)info->data + info->data_size)
    {
      grub_error (GRUB_ERR_BAD_OS, "truncated image");
      return GRUB_EFI_LOAD_ERROR;
    }

  grub_dprintf ("linux", "PE-COFF header checked\n");

  return GRUB_EFI_SUCCESS;
}

/**
 * load_sections() - load image sections into memory
 *
 * Allocate fresh memory and copy the image sections there.
 *
 * @info:	image information
 */
static grub_efi_status_t
load_sections (struct image_info *info)
{
  struct grub_pe32_section_table *section;
  unsigned long align_mask = 0xfff;

  /* Section alignment must be a power of two */
  if (info->section_alignment & (info->section_alignment - 1))
    {
      grub_error (GRUB_ERR_BAD_OS, "invalid section alignment");
      return GRUB_EFI_LOAD_ERROR;
    }

  if (info->section_alignment > align_mask)
    align_mask = info->section_alignment - 1;

  info->alloc_pages = GRUB_EFI_BYTES_TO_PAGES (info->loaded_image.image_size
					       + (align_mask & ~0xfffUL));

  info->alloc_addr = grub_efi_allocate_pages_real (
      GRUB_EFI_MAX_USABLE_ADDRESS, info->alloc_pages,
      GRUB_EFI_ALLOCATE_MAX_ADDRESS, GRUB_EFI_LOADER_CODE);
  if (!info->alloc_addr)
    return GRUB_EFI_OUT_OF_RESOURCES;

  info->loaded_image.image_base
      = (void *)(((unsigned long)info->alloc_addr + align_mask) & ~align_mask);

  grub_memcpy (info->loaded_image.image_base, info->data, info->header_size);
  for (section = &info->section[0];
       section < &info->section[info->num_sections]; ++section)
    {
      if (section->virtual_address < info->header_size
	  || (section->raw_data_size
	      && section->raw_data_offset < info->header_size))
	{
	  grub_error (GRUB_ERR_BAD_OS, "section inside header");
	  return GRUB_EFI_LOAD_ERROR;
	}
      if (section->raw_data_offset + section->raw_data_size > info->data_size)
	{
	  grub_error (GRUB_ERR_BAD_OS, "truncated image");
	  return GRUB_EFI_LOAD_ERROR;
	}
      if (section->virtual_address + section->virtual_size
	  > info->loaded_image.image_size)
	{
	  grub_error (GRUB_ERR_BAD_OS, "section outside image");
	  return GRUB_EFI_LOAD_ERROR;
	}

      grub_memset ((void *)((unsigned long)info->loaded_image.image_base
			    + section->virtual_address),
		   0, section->virtual_size);
      grub_memcpy (
	  (void *)((unsigned long)info->loaded_image.image_base
		   + section->virtual_address),
	  (void *)((unsigned long)info->data + section->raw_data_offset),
	  section->raw_data_size);
    }

  info->entry_point = (void *)((unsigned long)info->entry_point
			       + (unsigned long)info->loaded_image.image_base);

  grub_dprintf ("linux", "sections loaded\n");

  return GRUB_EFI_SUCCESS;
}

/**
 * lo12i_get() - get the immediate value of a format I instruction
 *
 * Instruction format I::
 *
 *     +---------------------------------------------------------------+
 *     |f e d c b a 9 8 7 6 5 4 3 2 1 0 f e d c b a 9 8 7 6 5 4 3 2 1 0|
 *     +-----------------------+---------+-----+---------+-------------+
 *     |       imm[11:0]       |   rs1   |fun3 |   rd    |   opcode    |
 *     +-----------------------+---------+-----+---------+-------------+
 *
 * @instr:	pointer to instruction
 * Return:	immediate value
 */
static grub_uint16_t
lo12i_get (grub_uint32_t *instr)
{
  return ((*instr & 0xfff00000) >> 20);
}

/**
 * lo12i_set() - set the immediate value of a format I instruction
 *
 * Instruction format I::
 *
 *     +---------------------------------------------------------------+
 *     |f e d c b a 9 8 7 6 5 4 3 2 1 0 f e d c b a 9 8 7 6 5 4 3 2 1 0|
 *     +-----------------------+---------+-----+---------+-------------+
 *     |       imm[11:0]       |   rs1   |fun3 |   rd    |   opcode    |
 *     +-----------------------+---------+-----+---------+-------------+
 *
 * @instr:	pointer to instruction
 * @imm:	immediate value
 */
static void
lo12i_set (grub_uint32_t *instr, grub_uint32_t imm)
{
  *instr = (*instr & 0x000fffff) | (imm & 0x00000fff << 20);
}

/**
 * hi20_get() - get the immediate value of a format I instruction
 *
 * Instruction format U::
 *
 *     +---------------------------------------------------------------+
 *     |f e d c b a 9 8 7 6 5 4 3 2 1 0 f e d c b a 9 8 7 6 5 4 3 2 1 0|
 *     +---------------------------------------+---------+-------------+
 *     |                imm[31:12]             |   rd    |   opcode    |
 *     +---------------------------------------+---------+-------------+
 *
 * @instr:	pointer to instruction
 * Return:	immediate value
 */
static grub_uint16_t
hi20_get (grub_uint32_t *instr)
{
  return *instr & 0xfffff000;
}

/**
 * hi20_set() - set the immediate value of a format I instruction
 *
 * Instruction format U::
 *
 *     +---------------------------------------------------------------+
 *     |f e d c b a 9 8 7 6 5 4 3 2 1 0 f e d c b a 9 8 7 6 5 4 3 2 1 0|
 *     +---------------------------------------+---------+-------------+
 *     |                imm[31:12]             |   rd    |   opcode    |
 *     +---------------------------------------+---------+-------------+
 *
 * @instr:	pointer to instruction
 * @imm:	immediate value
 */
static void
hi20_set (grub_uint32_t *instr, grub_uint32_t imm)
{
  *instr = (*instr & 0x00000fff) | (imm & 0xfffff000);
}

/**
 * lo12s_get() - get the immediate value of a format I instruction
 *
 * Instruction format S::
 *
 *     +---------------------------------------------------------------+
 *     |f e d c b a 9 8 7 6 5 4 3 2 1 0 f e d c b a 9 8 7 6 5 4 3 2 1 0|
 *     +-------------+---------+---------+-----+----+----+-------------+
 *     |  imm[11:5]  |   rs2   |   rs1   |fun3 |imm[4:0] |   opcode    |
 *     +-------------+---------+---------+-----+----+----+-------------+
 *
 * @instr:	pointer to instruction
 * Return:	immediate value
 */
static grub_uint16_t
lo12s_get (grub_uint32_t *instr)
{
  return ((*instr & 0x00000f80) >> 7) | ((*instr & 0xfe000000) >> 20);
}

/**
 * lo12s_set() - set the immediate value of a format I instruction
 *
 * Instruction format S::
 *
 *     +---------------------------------------------------------------+
 *     |f e d c b a 9 8 7 6 5 4 3 2 1 0 f e d c b a 9 8 7 6 5 4 3 2 1 0|
 *     +-------------+---------+---------+-----+----+----+-------------+
 *     |  imm[11:5]  |   rs2   |   rs1   |fun3 |imm[4:0] |   opcode    |
 *     +-------------+---------+---------+-----+----+----+-------------+
 *
 * @instr:	pointer to instruction
 * @imm:	immediate value
 */
static void
lo12s_set (grub_uint32_t *instr, grub_uint32_t imm)
{
  *instr = (*instr & 0x01fff07f) | (imm & 0x00000fe0 << 20)
	   | (imm & 0x0000001f << 7);
}

/**
 * movw_get_imm() - get the immediate value of MOVT and MOVW instructions
 *
 * MOVT::
 *
 *     +-------------------------------+-------------------------------+
 *     |f e d c b a 9 8 7 6 5 4 3 2 1 0|f e d c b a 9 8 7 6 5 4 3 2 1 0|
 *     +---------+-+-----------+-------+-+-----+-------+---------------+
 *     |1 1 1 1 0|i|1 0 1 1 0 0| imm4  |0| imm3|   Rd  |     imm8      |
 *     +---------+-+-----------+-------+-+-----+-------+---------------+
 *
 * MOVW::
 *
 *     +-------------------------------+-------------------------------+
 *     |f e d c b a 9 8 7 6 5 4 3 2 1 0|f e d c b a 9 8 7 6 5 4 3 2 1 0|
 *     +---------+-+-----------+-------+-+-----+-------+---------------+
 *     |1 1 1 1 0|i|1 0 0 1 0 0| imm4  |0| imm3|   Rd  |     imm8      |
 *     +---------+-+-----------+-------+-+-----+-------+---------------+
 *
 * @instr:	pointer to instruction
 * Return:	immediate value
 */
static grub_uint16_t
movw_get_imm (grub_uint16_t *instr)
{
  /* imm16 = imm4:i:imm3:imm8; */
  return (instr[1] & 0x00ff) | ((instr[1] & 0x7000) >> 3)
	 | ((instr[0] & 0x0400) >> 8) | ((instr[0] & 0x000f) << 12);
}

/**
 * movw_set_imm() - set the immediate value of MOVT and MOVW instructions
 *
 * MOVT::
 *
 *     +-------------------------------+-------------------------------+
 *     |f e d c b a 9 8 7 6 5 4 3 2 1 0|f e d c b a 9 8 7 6 5 4 3 2 1 0|
 *     +---------+-+-----------+-------+-+-----+-------+---------------+
 *     |1 1 1 1 0|i|1 0 1 1 0 0| imm4  |0| imm3|   Rd  |     imm8      |
 *     +---------+-+-----------+-------+-+-----+-------+---------------+
 *
 * MOVW::
 *
 *     +-------------------------------+-------------------------------+
 *     |f e d c b a 9 8 7 6 5 4 3 2 1 0|f e d c b a 9 8 7 6 5 4 3 2 1 0|
 *     +---------+-+-----------+-------+-+-----+-------+---------------+
 *     |1 1 1 1 0|i|1 0 0 1 0 0| imm4  |0| imm3|   Rd  |     imm8      |
 *     +---------+-+-----------+-------+-+-----+-------+---------------+
 *
 * @instr:	pointer to instruction
 * @imm		immediate value
 */
static void
movw_set_imm (grub_uint16_t *instr, grub_uint16_t imm)
{
  /* imm16 = imm4:i:imm3:imm8; */
  instr[0] = (instr[0] & 0xfbf0) | (imm & 0xf000) >> 12 | (imm & 0x0800) << 3;
  instr[1] = (instr[0] & 0x8f00) | (imm & 0xff) | (imm & 0x0700) >> 4;
}

/**
 * relocate() - apply relocations to the image
 *
 * @info:	information about the loaded image
 */
static grub_efi_status_t
relocate (struct image_info *info)
{
  struct grub_pe32_fixup_block *block, *reloc_end;
  unsigned long offset;
  grub_uint16_t reloc_type;
  grub_uint16_t *reloc_entry;
  grub_uint32_t *rvhi20_addr = NULL;

  if (!info->reloc || !(info->reloc->size))
    {
      grub_dprintf ("linux", "no relocations\n");
      return GRUB_EFI_SUCCESS;
    }

  if (info->reloc->rva + info->reloc->size > info->loaded_image.image_size)
    {
      grub_error (GRUB_ERR_BAD_OS, "relocation block outside image");
      return GRUB_EFI_LOAD_ERROR;
    }

  /*
   * The relocations are based on the difference between
   * actual load address and the preferred base address.
   */
  offset = (unsigned long)info->loaded_image.image_base - info->image_base;

  block = (void *)((unsigned long)info->loaded_image.image_base
		   + info->reloc->rva);
  reloc_end = (void *)((unsigned long)block + info->reloc->size);

  for (; block < reloc_end;
       block = (void *)((unsigned long)block + block->block_size))
    {
      reloc_entry = block->entries;
      grub_uint16_t *block_end
	  = (void *)((unsigned long)block + block->block_size);

      for (; reloc_entry < block_end; ++reloc_entry)
	{
	  void *addr = (void *)((unsigned long)info->loaded_image.image_base
				+ block->page_rva + (*reloc_entry & 0xfff));

	  reloc_type = *reloc_entry >> 12;

	  switch (reloc_type)
	    {
	    case GRUB_PE32_REL_BASED_ABSOLUTE:
	      /* skip */
	      break;
	    case GRUB_PE32_REL_BASED_HIGH:
	      *(grub_uint16_t *)addr += offset >> 16;
	      break;
	    case GRUB_PE32_REL_BASED_LOW:
	      *(grub_uint16_t *)addr += offset;
	      break;
	    case GRUB_PE32_REL_BASED_HIGHLOW:
	      *(grub_uint32_t *)addr += offset;
	      break;
	    case GRUB_PE32_REL_BASED_RISCV_HI20:
	      switch (info->machine)
		{
		case GRUB_PE32_MACHINE_RISCV32:
		case GRUB_PE32_MACHINE_RISCV64:
		  rvhi20_addr = addr;
		  break;
		default:
		  goto bad_reloc;
		}
	      break;
	    case GRUB_PE32_REL_BASED_ARM_MOV32T:
	      /* = GRUB_PE32_REL_BASED_RISCV_LOW12I */
	      switch (info->machine)
		{
		case GRUB_PE32_MACHINE_ARMTHUMB_MIXED:
		  {
		    grub_uint16_t *instr = addr;
		    grub_uint32_t val;

		    val = movw_get_imm (&instr[0])
			  + (movw_get_imm (&instr[2]) << 16) + offset;
		    movw_set_imm (&instr[0], val);
		    movw_set_imm (&instr[2], val >> 16);
		    break;
		  }
		case GRUB_PE32_MACHINE_RISCV32:
		case GRUB_PE32_MACHINE_RISCV64:
		  if (rvhi20_addr)
		    {
		      grub_uint32_t val
			  = hi20_get (rvhi20_addr) + lo12i_get (addr) + offset;
		      hi20_set (rvhi20_addr, val);
		      lo12i_set (addr, val);
		      rvhi20_addr = NULL;
		    }
		  else
		    {
		      goto bad_reloc;
		    }
		  break;
		default:
		  goto bad_reloc;
		}
	      break;
	    case GRUB_PE32_REL_BASED_RISCV_LOW12S:
	      switch (info->machine)
		{
		case GRUB_PE32_MACHINE_RISCV32:
		case GRUB_PE32_MACHINE_RISCV64:
		  if (rvhi20_addr)
		    {
		      grub_uint32_t val
			  = hi20_get (rvhi20_addr) + lo12s_get (addr) + offset;
		      hi20_set (rvhi20_addr, val);
		      lo12s_set (addr, val);
		      rvhi20_addr = NULL;
		    }
		  else
		    {
		      goto bad_reloc;
		    }
		  break;
		default:
		  goto bad_reloc;
		}
	      break;
	    case GRUB_PE32_REL_BASED_DIR64:
	      *(grub_uint64_t *)addr += offset;
	      break;
	    default:
	      goto bad_reloc;
	    }
	}
    }

  grub_dprintf ("linux", "image relocated\n");

  return GRUB_EFI_SUCCESS;

bad_reloc:
  grub_error (GRUB_ERR_BAD_OS, "unsupported relocation type %d, rva 0x%08lx\n",
	      *reloc_entry >> 12,
	      (unsigned long)reloc_entry
		  - (unsigned long)info->loaded_image.image_base);
  return GRUB_EFI_LOAD_ERROR;
}

/**
 * exit_hook() - replacement for EFI_BOOT_SERVICES.Exit()
 *
 * This function is inserted into system table to trap invocations of
 * EFI_BOOT_SERVICES.Exit(). If Exit() is called with our handle
 * return to our start routine using a long jump.
 *
 * @image_handle:	handle of the application as passed on entry
 * @exit_status:	the images exit code
 * @exit_data_size:	size of @exit_data
 * @exit_data:		null terminated string followed by optional data
 */
static grub_efi_status_t __grub_efi_api
exit_hook (grub_efi_handle_t image_handle, grub_efi_status_t exit_status,
	   grub_efi_uintn_t exit_data_size, grub_efi_char16_t *exit_data)
{
  struct image_info *info;

  if (!image_handle)
    return GRUB_EFI_INVALID_PARAMETER;

  info = grub_efi_open_protocol (image_handle,
				 &(grub_guid_t)GRUB_PEIMAGE_MARKER_GUID,
				 GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (!info)
    {
      grub_dprintf ("linux", "delegating Exit()\n");
      return exit_orig (image_handle, exit_status, exit_data_size,
			(grub_efi_char16_t *)exit_data);
    }

  info->exit_status = exit_status;

  if (exit_status != GRUB_EFI_SUCCESS)
    {
      grub_printf ("Application failed, r = %d\n",
		   (int)exit_status & 0x7fffffff);
      if (exit_data_size && exit_data)
	{
	  grub_printf ("exit message: ");
	  for (grub_efi_uintn_t pos = 0;
	       exit_data[pos] && pos < exit_data_size / 2; ++pos)
	    grub_printf ("%C", exit_data[pos]);
	  grub_printf ("\n");
	}
    }
  if (exit_data_size && exit_data)
    {
      /* exit data must be freed by the caller */
      grub_efi_system_table->boot_services->free_pool (exit_data);
    }
  grub_longjmp (info->jmp, 1);
}

/**
 * unload_image_hook() - replacement for EFI_BOOT_SERVICES.UnloadImage()
 *
 * peimage only supports loading EFI Applications, which cannot be correctly
 * unloaded while running.
 *
 * Installing this hooks prevents that from happening.
 */
static grub_efi_status_t __grub_efi_api
unload_image_hook (grub_efi_handle_t image_handle)
{
  if (grub_efi_open_protocol (image_handle,
         &(grub_guid_t) GRUB_PEIMAGE_MARKER_GUID,
         GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL))
    return GRUB_EFI_UNSUPPORTED;

  return unload_image_orig(image_handle);
}

static grub_efi_status_t __grub_efi_api
do_load_image (grub_efi_boolean_t boot_policy __attribute__ ((unused)),
	       grub_efi_handle_t parent_image_handle,
	       grub_efi_device_path_t *file_path, void *source_buffer,
	       grub_efi_uintn_t source_size, grub_efi_handle_t *image_handle)
{
  grub_efi_status_t status;
  struct image_info *info;

  info = grub_efi_allocate_pages_real (
      GRUB_EFI_MAX_USABLE_ADDRESS, GRUB_EFI_BYTES_TO_PAGES (sizeof *info),
      GRUB_EFI_ALLOCATE_MAX_ADDRESS, GRUB_EFI_LOADER_DATA);
  if (!info)
    return GRUB_EFI_OUT_OF_RESOURCES;

  grub_memset (info, 0, sizeof *info);

  info->data = source_buffer;
  info->data_size = source_size;

  // Save original device path
  if (file_path)
    info->orig_file_path = grub_efi_duplicate_device_path (file_path);

  // Split out file path
  while (file_path
	 && (file_path->type != GRUB_EFI_MEDIA_DEVICE_PATH_TYPE
	     || file_path->subtype != GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE))
    file_path = GRUB_EFI_NEXT_DEVICE_PATH (file_path);
  if (file_path)
    info->loaded_image.file_path = grub_efi_duplicate_device_path (file_path);

  // Process PE image
  status = check_pe_header (info);
  if (status != GRUB_EFI_SUCCESS)
    goto err;

  status = load_sections (info);
  if (status != GRUB_EFI_SUCCESS)
    goto err;

  status = relocate (info);
  if (status != GRUB_EFI_SUCCESS)
    goto err;

  // Setup EFI_LOADED_IMAGE_PROTOCOL
  info->loaded_image.revision = GRUB_EFI_LOADED_IMAGE_REVISION;
  info->loaded_image.parent_handle = parent_image_handle;
  info->loaded_image.system_table = grub_efi_system_table;
  info->loaded_image.image_code_type = GRUB_EFI_LOADER_CODE;
  info->loaded_image.image_data_type = GRUB_EFI_LOADER_DATA;

  // Instruct EFI to create a new handle
  *image_handle = NULL;
  status
      = grub_efi_system_table->boot_services
	    ->install_multiple_protocol_interfaces (
		image_handle, &(grub_guid_t)GRUB_PEIMAGE_MARKER_GUID, info,
		&(grub_guid_t)GRUB_EFI_LOADED_IMAGE_GUID, &info->loaded_image,
		&(grub_guid_t)GRUB_EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID,
		info->orig_file_path, NULL);
  if (status != GRUB_EFI_SUCCESS)
    goto err;

  // Increment module refcount
  grub_dl_ref (my_mod);

  return status;

err:
  if (info->alloc_addr)
    grub_efi_free_pages ((unsigned long)info->alloc_addr, info->alloc_pages);
  if (info->loaded_image.file_path)
    grub_free (info->loaded_image.file_path);
  if (info->orig_file_path)
    grub_free (info->orig_file_path);
  grub_efi_free_pages ((unsigned long)info,
		       GRUB_EFI_BYTES_TO_PAGES (sizeof *info));
  return status;
}

static grub_efi_status_t __grub_efi_api
do_start_image (grub_efi_handle_t image_handle,
		grub_efi_uintn_t *exit_data_size __attribute__ ((unused)),
		grub_efi_char16_t **exit_data __attribute__ ((unused)))
{
  grub_efi_status_t status;
  struct image_info *info;

  info = grub_efi_open_protocol (image_handle,
				 &(grub_guid_t)GRUB_PEIMAGE_MARKER_GUID,
				 GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (!info)
    {
      grub_error (GRUB_ERR_BAD_OS, "image not loaded");
      return GRUB_EFI_LOAD_ERROR;
    }

  if (grub_setjmp (info->jmp))
    {
      status = info->exit_status;
      do_unload_image (image_handle);
    }
  else
    {
      grub_dprintf ("linux",
		    "Executing image loaded at 0x%lx\n"
		    "Entry point 0x%lx\n"
		    "Size 0x%lx\n",
		    (unsigned long)info->loaded_image.image_base,
		    (unsigned long)info->entry_point,
		    (unsigned long)info->loaded_image.image_size);

      /* Invalidate the instruction cache */
      grub_arch_sync_caches (info->loaded_image.image_base,
			     info->loaded_image.image_size);

      status = info->entry_point (image_handle, grub_efi_system_table);

      grub_dprintf ("linux", "Application returned\n");

      /* converge to the same exit path as if the image called
       * boot_services->exit itself */
      exit_hook (image_handle, status, 0, NULL);

      /* image_handle is always valid above, thus exit_hook cannot
       * return to here. if only GRUB had assert :(
      grub_assert (false && "entered unreachable code");
      */
    }

  return status;
}

static grub_efi_status_t __grub_efi_api
do_unload_image (grub_efi_handle_t image_handle)
{
  grub_efi_status_t status;
  struct image_info *info;

  info = grub_efi_open_protocol (image_handle,
				 &(grub_guid_t)GRUB_PEIMAGE_MARKER_GUID,
				 GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (!info)
    {
      grub_error (GRUB_ERR_BAD_OS, "image not loaded");
      return GRUB_EFI_LOAD_ERROR;
    }

  status
      = grub_efi_system_table->boot_services
	    ->uninstall_multiple_protocol_interfaces (
		image_handle, &(grub_guid_t)GRUB_PEIMAGE_MARKER_GUID, info,
		&(grub_guid_t)GRUB_EFI_LOADED_IMAGE_GUID, &info->loaded_image,
		&(grub_guid_t)GRUB_EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID,
		info->orig_file_path, NULL);
  if (status != GRUB_EFI_SUCCESS)
    return GRUB_EFI_LOAD_ERROR;

  if (info->alloc_addr)
    grub_efi_free_pages ((unsigned long)info->alloc_addr, info->alloc_pages);
  if (info->loaded_image.file_path)
    grub_free (info->loaded_image.file_path);
  if (info->orig_file_path)
    grub_free (info->orig_file_path);

  grub_efi_free_pages ((unsigned long)info,
		       GRUB_EFI_BYTES_TO_PAGES (sizeof *info));

  grub_dl_unref (my_mod);

  return GRUB_EFI_SUCCESS;
}

static const grub_efi_loader_t peimage_loader = {
  .load_image = do_load_image,
  .start_image = do_start_image,
  .unload_image = do_unload_image,
};

GRUB_MOD_INIT (peimage)
{
  grub_efi_register_loader (&peimage_loader);
  my_mod = mod;

  // Hook exit handler
  exit_orig = grub_efi_system_table->boot_services->exit;
  grub_efi_system_table->boot_services->exit = exit_hook;
  // Hook unload_image handler
  unload_image_orig = grub_efi_system_table->boot_services->unload_image;
  grub_efi_system_table->boot_services->unload_image = unload_image_hook;
}

GRUB_MOD_FINI (peimage)
{
  // Restore exit handler
  grub_efi_system_table->boot_services->exit = exit_orig;
  // Restore unload_image handler
  grub_efi_system_table->boot_services->unload_image = unload_image_orig;

  grub_efi_unregister_loader (&peimage_loader);
}
