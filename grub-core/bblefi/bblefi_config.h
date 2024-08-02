//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#ifndef __BARA__BBL_CONFIG__
#define __BARA__BBL_CONFIG__ 1

#include "bara_efi_defines.h"

#define MAX_SERVERS 8
#define MAX_PROPERTYNAMELENGTH 20
#define MAX_PROPERTYVALUELENGTH 512
#define MAX_PROPERTIES 32

int isdebug(void);
void debugdelay(const int seconds);
grub_err_t read_bblefi_config(const grub_efi_pxe_ip_address_t* tftpIp,
							  const char* servers[],
							  const char* properties[],
							  const char* values[],
							  int* noOfServer);
const char* valueofproperty(const char* const properties[], const char* const values[], const char* const name);

#endif
