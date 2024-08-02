//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#ifndef __BARAEFI__
#define __BARAEFI__ 1

#include <grub/efi/api.h>

#define FALSE 0
#define TRUE 1

#define MENU_ENTRIES_MAX 20
#define SERVERNMAE_LENGTH_MAX 128

#pragma pack(4)
typedef union bara_efi_ip_address
{
	grub_uint32_t addr[4];
	grub_uint32_t ipv4;
	grub_uint64_t ipv6[2];
} bara_efi_ip_address_t;
#pragma pack()

typedef enum {
    EFI_PXE_BASE_CODE_TFTP_FIRST,
    EFI_PXE_BASE_CODE_TFTP_GET_FILE_SIZE,
    EFI_PXE_BASE_CODE_TFTP_READ_FILE,
    EFI_PXE_BASE_CODE_TFTP_WRITE_FILE,
    EFI_PXE_BASE_CODE_TFTP_READ_DIRECTORY,
    EFI_PXE_BASE_CODE_MTFTP_GET_FILE_SIZE,
    EFI_PXE_BASE_CODE_MTFTP_READ_FILE,
    EFI_PXE_BASE_CODE_MTFTP_READ_DIRECTORY,
    EFI_PXE_BASE_CODE_MTFTP_LAST
} bara_efi_pxe_base_code_tftp_opcode_t;

typedef grub_efi_status_t (*bara_efi_pxe_base_code_mtftp)
(
		grub_efi_pxe_t *this,
		bara_efi_pxe_base_code_tftp_opcode_t operation,
		void* bufferptr,
		grub_efi_boolean_t overwrite,
		grub_uint64_t *buffersize,
		grub_efi_uintn_t *blockSize,
		const bara_efi_ip_address_t* const serverip,
		const char* const filename,
		void* info,
		grub_efi_boolean_t dontusebuffer
);

//typedef grub_efi_status_t (*setPacketsWithParams) (
//                     struct grub_efi_pxe *This,
//                     grub_efi_boolean_t *NewDhcpDiscoverValid,
//                     grub_efi_boolean_t *NewDhcpAckReceived,
//                     grub_efi_boolean_t *NewProxyOfferReceived,
//                     grub_efi_boolean_t *NewPxeDiscoverValid,
//                     grub_efi_boolean_t *NewPxeReplyReceived,
//                     grub_efi_boolean_t *NewPxeBisReplyReceived,
//                     grub_efi_pxe_packet_t *NewDhcpDiscover,
//                     grub_efi_pxe_packet_t *NewDhcpAck,
//                     grub_efi_pxe_packet_t *NewProxyOffer,
//                     grub_efi_pxe_packet_t *NewPxeDiscover,
//                     grub_efi_pxe_packet_t *NewPxeReply,
//                     grub_efi_pxe_packet_t *NewPxeBisReply);

#endif
