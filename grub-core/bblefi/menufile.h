//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#ifndef __BARA_MENUFILE__
#define __BARA_MENUFILE__ 1

#include "bara_efi_defines.h"

/* C prototypes */
typedef enum
{
	eBLMETInvalid = 0,

	eBLMETTimeout,
	eBLMETServerName,

	eBLMETText,
	eBLMETSyslinuxLocalHDD,
	eBLMETSyslinuxPrompt,
	eBLMETSyslinuxCommand,
	eBLMETSyslinuxChainload,
	eBLMETGrubWinPEChainload,
	eBLMETGrubPrompt,
} EBootLoaderMenuEntryType;

typedef enum
{
	eBLMEFNone                  = 0x00000000,
	eBLMEFDefault               = 0x00000001,
	eBLMEFAutoRegisterClient    = 0x00000002,
	eBLMEFUpdateBootEnv         = 0x00000004,
	eBLMEFSetTFTPPathPrefix     = 0x00000008,
} EBootLoaderMenuEntryFlags;

#define MAX_BOOTLOADERMENUENTRYTEXTLEN 80
#define MAX_BOOTLOADERMENUENTRYCOMMANDLEN 512 /* Opportunity to get configuration data into memdisk DOS image */
#define MAX_BOOTLOADERMENUENTRYGUIDLEN 40
#define MAX_BOOTLOADERMENUENTRYPATHPREFIX 80

typedef struct s_BootLoaderMenuEntry
{
	EBootLoaderMenuEntryType m_eType;
	EBootLoaderMenuEntryFlags m_eFlags;
	char m_szText[MAX_BOOTLOADERMENUENTRYTEXTLEN + 1];
	char m_szCommand[MAX_BOOTLOADERMENUENTRYCOMMANDLEN + 1];
	char m_szBootEnvGUID[MAX_BOOTLOADERMENUENTRYGUIDLEN + 1];
	char m_szTFTPPathPrefix[MAX_BOOTLOADERMENUENTRYPATHPREFIX + 1];
} BootLoaderMenuEntryT;

grub_err_t LoadBootLoaderMenuFile(grub_efi_pxe_ip_address_t* server_ip, grub_efi_mac_t* client_mac, const grub_uint16_t arch_type,
							BootLoaderMenuEntryT* const entries, const int max_entries, const int retries,	const int max_delay);

#endif
