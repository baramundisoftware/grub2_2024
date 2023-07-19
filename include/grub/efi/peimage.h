/* SPDX-License-Identifier: GPL-3.0+ */

#define GRUB_PE_MAGIC "PE\0"

#define EFI_LOADED_IMAGE_PROTOCOL_REVISION (0x1000)

typedef grub_efi_status_t (*grub_efi_entry_point) (
    grub_efi_handle_t image_handle, grub_efi_system_table_t *system_table);

struct grub_dos_stub
{
  grub_uint16_t magic;	    /* MZ magic */
  grub_uint16_t lbsize;	    /* size of last block */
  grub_uint32_t res0;	    /* reserved */
  grub_uint64_t res1;	    /* reserved */
  grub_uint64_t res2;	    /* reserved */
  grub_uint64_t res3;	    /* reserved */
  grub_uint64_t res4;	    /* reserved */
  grub_uint64_t res5;	    /* reserved */
  grub_uint64_t res6;	    /* reserved */
  grub_uint32_t linux_arch; /* linux architecture */
  grub_uint32_t pe_addr;    /* offset of PE/COFF header */
};

typedef struct grub_efi_boot_services_image
{
  grub_efi_status_t (*load_image) (grub_efi_boolean_t boot_policy,
				   grub_efi_handle_t parent_image_handle,
				   grub_efi_device_path_t *file_path,
				   void *source_buffer,
				   grub_efi_uintn_t source_size,
				   grub_efi_handle_t *image_handle);

  grub_efi_status_t (*start_image) (grub_efi_handle_t image_handle,
				    grub_efi_uintn_t *exit_data_size,
				    grub_efi_char16_t **exit_data);

  grub_efi_status_t (*unload_image) (grub_efi_handle_t image_handle);
} grub_efi_boot_services_image_t;

extern grub_efi_boot_services_image_t
    EXPORT_VAR (grub_efi_boot_services_peimage);
