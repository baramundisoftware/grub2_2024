//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#ifndef __BARA__UTILS__
#define __BARA__UTILS__ 1

#include <grub/efi/api.h>
#include "bara_efi_defines.h"

grub_err_t getClientUuid(const char **uuid);

grub_err_t getClientMac(grub_efi_mac_t* client_mac);
grub_err_t printEfiMacAddress(grub_efi_mac_t* mac_address);
grub_err_t convertEfiMacToString(grub_efi_mac_t* mac_address, char* mac_string);

grub_err_t getDhcpServerIp(grub_efi_pxe_ip_address_t* server_ip);
grub_err_t printEfiIpv4Address(const grub_efi_pxe_ip_address_t* const  ip_address);

grub_err_t getDhcpProcessorArchitectureType(grub_uint16_t* arch_type);

grub_efi_status_t tftpReadFile(const grub_efi_pxe_ip_address_t* serverip, const char* const filename, void* buffer, grub_uint64_t buffersize);
grub_efi_status_t tftpGetFileSize(const grub_efi_pxe_ip_address_t* serverip, const char* const filename, grub_uint64_t* filesize);

grub_err_t chainload_uefi_winpe(const char* new_bootfilename, const grub_efi_pxe_ip_address_t* const server_ip, const grub_efi_boolean_t always_use_secureboot_chainload);
grub_err_t changeDhcpAckParms(const char* new_bootfilename, const grub_efi_pxe_ip_address_t* const server_ip);

int mac_to_int(grub_efi_mac_t* mac_address);

int dnsRequestServerIp(const char* const newServerIp, grub_efi_pxe_ip_address_t* ipaddr);

void trim(char* const s);

int getMACAdress(char* mac);

int getServerIp(char** serverIp);

int dumpDhcpDiscoverOptions(void);

int getkey(const unsigned long timeoutMilliSeconds);

int waitforkey(const int key, const unsigned long delay_seconds);

int grub_quit(void);

void srandom(unsigned int seed);

long int random(void);

int changeStartServer(const char* const newServerAddress);

int ip_to_int (const char * const ip);

//grub_err_t convertIpToString(grub_efi_pxe_ip_address_t* src, char* ipAsString);

//void switchGrubEnvironment(const char* server, const int bootFromMenuServer);

#endif
