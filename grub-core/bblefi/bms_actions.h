//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#ifndef __BARA__BMS_ACTIONS__
#define __BARA__BMS_ACTIONS__ 1

#include "bara_efi_defines.h"

grub_err_t loaddynamicbootmenu(grub_efi_pxe_ip_address_t* server_ip, grub_efi_mac_t* client_mac, const grub_uint16_t arch_type, char** buffer);
grub_err_t updateclientbootenv(const grub_efi_pxe_ip_address_t* const server_ip, grub_efi_mac_t* client_mac, const char* const bootEnvGUID);
grub_err_t setpathprefix(const grub_efi_pxe_ip_address_t* const server_ip, grub_efi_mac_t* client_mac, const char* const bootEnvGUID, const char* const pathPrefix);

#endif
