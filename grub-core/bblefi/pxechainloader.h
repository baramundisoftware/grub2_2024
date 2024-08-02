//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#ifndef __BARA_PXECHAIN__
#define __BARA_PXECHAIN__ 1

#include "bara_efi_defines.h"
grub_err_t pxechainloader(char *filename, const grub_efi_pxe_ip_address_t* const serverip, const grub_efi_boolean_t always_use_secureboot_chainload);

#endif
