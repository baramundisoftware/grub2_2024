//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#ifndef __BARA_MENU__
#define __BARA_MENU__ 1

#include "menufile.h"

grub_err_t menu(grub_efi_pxe_ip_address_t* server_ip, grub_efi_mac_t* client_mac, const grub_uint16_t arch_type,
		const int menuRetries, const BootLoaderMenuEntryT** chosenEntry, int* const defaultchosen, int* const choice);

#endif
