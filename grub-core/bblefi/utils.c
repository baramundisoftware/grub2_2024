//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/net.h>
#include <grub/mm.h>
#include <grub/term.h>
#include <grub/time.h>
#include <grub/command.h>
#include <grub/dl.h>
#include "bblefi_config.h"
#include "bara_efi_defines.h"
#include "utils.h"
#include "pxechainloader.h"
#include <grub/env.h>
#include <grub/extcmd.h>

#define UNUSED(expr) do { (void)(expr); } while (0)

GRUB_MOD_LICENSE("GPLv3+");

#define BARA_UUID_ENV_VARIABLE "bara_smbios_uuid"

/*******************************************************************************************************************/
static grub_guid_t pxe_io_guid = GRUB_EFI_PXE_GUID;
static grub_guid_t net_io_guid = GRUB_EFI_SIMPLE_NETWORK_GUID;

grub_uint8_t b_ntohs(grub_uint16_t x);

grub_efi_pxe_t* getEfiPxeBaseCodeProtocol(void);

int parseDhcpDiscoverOptions(int (*option_handler)(const grub_uint8_t, const grub_uint8_t, const grub_uint8_t*, void*), void* outdata);
int dumpOption(const grub_uint8_t option_number, const grub_uint8_t option_data_length, const grub_uint8_t* data, void*);
int architectureOption(const grub_uint8_t option_number, const grub_uint8_t option_data_length, const grub_uint8_t* data, void* arch_type);

// seed for the random function
static grub_int32_t rnd_seed = 0;

/*******************************************************************************************************************/

// get a pointer to the EFI_PXE_BASE_CODE_PROTOCOL data for the actual device
grub_efi_pxe_t* getEfiPxeBaseCodeProtocol(void)
{
	grub_efi_pxe_t* pxe = NULL;

	grub_efi_loaded_image_t *image = grub_efi_get_loaded_image (grub_efi_image_handle);

	if (image)
	{
		pxe = grub_efi_open_protocol (image->device_handle, &pxe_io_guid, GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

		if(!pxe && isdebug())
		{
			grub_printf("Failed to open pxe base code protocol.\n");
		}
	}
	else if (isdebug())
	{
		grub_printf("Failed to get loaded image.");
	}

	return pxe;
}


grub_efi_status_t tftpGetFileSize(const grub_efi_pxe_ip_address_t* server_ip, const char* const filename, grub_uint64_t* filesize)
{
	grub_efi_status_t status = GRUB_EFI_SUCCESS;
	grub_efi_pxe_t* pxe = getEfiPxeBaseCodeProtocol();

	if(!pxe)
	{
		return GRUB_EFI_NOT_FOUND;
	}

	grub_efi_boolean_t overwrite = FALSE;
	grub_efi_boolean_t nobuffer = FALSE;
	grub_efi_uintn_t blocksize = 1456;

	*filesize = 0;

	status = pxe->mtftp(pxe, GRUB_EFI_PXE_BASE_CODE_TFTP_GET_FILE_SIZE, NULL, overwrite,
							filesize, &blocksize, (grub_efi_pxe_ip_address_t*) server_ip, (unsigned char *) filename, NULL, nobuffer);

	return status;
}

grub_efi_status_t tftpReadFile(const grub_efi_pxe_ip_address_t* server_ip, const char* const filename, void* buffer, grub_uint64_t buffersize)
{
	grub_efi_status_t status = GRUB_EFI_SUCCESS;
	grub_efi_pxe_t* pxe = getEfiPxeBaseCodeProtocol();

	if(!pxe || !buffer)
	{
		return GRUB_EFI_NOT_FOUND;
	}

	grub_efi_boolean_t overwrite = FALSE;
	grub_efi_boolean_t nobuffer = FALSE;
	grub_efi_uintn_t blocksize = 1456;

	status = pxe->mtftp(pxe, GRUB_EFI_PXE_BASE_CODE_TFTP_READ_FILE, buffer, overwrite,
							&buffersize, &blocksize, (grub_efi_pxe_ip_address_t*) server_ip, (unsigned char*) filename, NULL, nobuffer);

	return status;
}

int dnsRequestServerIp(const char* const newServerIp, grub_efi_pxe_ip_address_t* ipaddr)
{
	grub_memset(ipaddr, 0, sizeof(grub_efi_pxe_ip_address_t));
	ipaddr->addr[0] = 0;
	unsigned int retries;

	if (NULL != newServerIp && 0 != grub_strcmp(newServerIp, ""))
	{
		grub_uint32_t ip = ip_to_int(newServerIp);

		if ((grub_uint32_t)-1 != ip)
		{
			//TODO: validate
			ipaddr->addr[0] = ip;
		}
		else
		{
			// If address is not a valid ip, it can be a hostname.
			// In this case it is nescessary to perform a dns-lookup.
			grub_net_network_level_address_t addr;
			grub_err_t error = grub_net_resolve_address(newServerIp, &addr);

			// It happens that the net device is not ready yet, so we have to retry.
			for (retries = 10; retries > 0 && error == GRUB_ERR_WAIT; retries--)
			{
				grub_sleep(1);
				error = grub_net_resolve_address(newServerIp, &addr);
			}

			if (GRUB_ERR_NONE == error)
			{
				ipaddr->addr[0] = addr.ipv4;

				if(isdebug())
				{
					grub_printf("Server address was successfully resolved: %d \n", addr.ipv4);
				}
			}
			else
			{
				grub_error(GRUB_ERR_NET_NO_ANSWER, "Server FQDN [%s] could not be resolved to a valid IP-Address (DNS)", newServerIp);
				return -1;
			}
		}

		return GRUB_ERR_NONE;
	}
	return -1;
}

grub_err_t changeDhcpAckParms(const char* new_bootfilename, const grub_efi_pxe_ip_address_t* const server_ip)
{
	grub_errno = GRUB_ERR_NONE;

	if(!new_bootfilename || !server_ip)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		grub_efi_pxe_t *pxe = getEfiPxeBaseCodeProtocol();

		if(!pxe || !pxe->mode)
		{
			grub_error(GRUB_ERR_BAD_MODULE, "cannot open pxe base code protocol!");
		}
		else
		{
			if(pxe->mode->pxe_reply_received)
			{
				pxe->mode->pxe_reply_received = 0;
				struct grub_net_bootp_packet* pxe_reply_packet = (struct grub_net_bootp_packet*)&pxe->mode->pxe_reply;
				grub_memset(pxe_reply_packet->boot_file, 0, sizeof(pxe_reply_packet->boot_file));
				pxe_reply_packet->server_ip = 0;
			}

			if(pxe->mode->proxy_offer_received)
			{
				pxe->mode->proxy_offer_received = 0;
				struct grub_net_bootp_packet* proxy_offer_packet = (struct grub_net_bootp_packet*)&pxe->mode->proxy_offer;
				grub_memset(proxy_offer_packet->boot_file, 0, sizeof(proxy_offer_packet->boot_file));
				proxy_offer_packet->server_ip = 0;
			}

			if(pxe->mode->dhcp_ack_received)
			{
				struct grub_net_bootp_packet* dhcp_ack_packet = (struct grub_net_bootp_packet*)&pxe->mode->dhcp_ack;
				grub_memset(dhcp_ack_packet->boot_file, 0, sizeof(dhcp_ack_packet->boot_file));

				// set new bootfilename
				grub_size_t nSizeNewName = grub_strlen(new_bootfilename) * sizeof(char);
				grub_memcpy(dhcp_ack_packet->boot_file, new_bootfilename,
							nSizeNewName < sizeof(dhcp_ack_packet->boot_file) ? nSizeNewName : sizeof(dhcp_ack_packet->boot_file));

				if(isdebug())
				{
					grub_printf("BootFileName changed to: %s \n", dhcp_ack_packet->boot_file);
				}

				if(server_ip->addr)
				{
					grub_memcpy(&dhcp_ack_packet->server_ip, &server_ip->addr, 4);

					if (isdebug())
					{
						grub_printf("BootServer-IP changed to: ");
						printEfiIpv4Address(server_ip);
						grub_printf("\n");
					}
				}
				else
				{
					grub_error(GRUB_ERR_BAD_ARGUMENT, "server ip is not valid!");
				}
			}
			else
			{
				grub_error(GRUB_ERR_BAD_MODULE, "no dhcp packet recieved!");
			}

			debugdelay(5);
		}
	}

	return grub_errno;
}

//int changeStartServer(const char* const newServerAddress)
//{
//	int ret = -1;
//
//	grub_efi_pxe_t *pxe = getEfiPxeBaseCodeProtocol();
//
//	if (pxe) {
//		grub_efi_pxe_mode_t *pxe_mode = pxe->mode;
//
//		struct grub_net_bootp_packet* ack_packet = (struct grub_net_bootp_packet *) &pxe_mode->dhcp_ack;
//
//		int ip = ip_to_int(newServerAddress);
//
//		ack_packet->server_ip = ip;
//
//		setPacketsWithParams setValue = (setPacketsWithParams) pxe->setpackets;
//
//		setValue(pxe, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &pxe_mode->dhcp_ack, NULL, NULL, NULL, NULL);
//
//		grub_printf("Server-Ip changed to: %s \n", newServerAddress);
//
//		ret = 0;
//	}
//
//	return ret;
//}

int ip_to_int (const char * const ip)
{
    /* The return value. */
    unsigned v = 0;
    /* The count of the number of bytes processed. */
    int i;
    /* A pointer to the next digit to process. */
    const char * start;

    start = ip;
    for (i = 0; i < 4; i++) {
        /* The digit being processed. */
        char c;
        /* The value of this byte. */
        int n = 0;
        while (1) {
            c = * start;
            start++;
            if (c >= '0' && c <= '9') {
                n *= 10;
                n += c - '0';
            }
            /* We insist on stopping at "." if we are still parsing
               the first, second, or third numbers. If we have reached
               the end of the numbers, we will allow any character. */
            else if ((i < 3 && c == '.') || i == 3) {
                break;
            }
            else {
                return -1;
            }
        }
        if (n >= 256) {
            return -1;
        }
        v += n << (8*i);
    }
    return v;
}


grub_err_t getClientUuid(const char **uuid)
{
	grub_errno = GRUB_ERR_NONE;
	const char *cmd = "smbios";
	const char *args[6] = {"-u", "8", "-t", "1", "--set", BARA_UUID_ENV_VARIABLE};

	if(!uuid)
	{
		return grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}

	if (grub_command_execute (cmd, 6, (char**) args))
		return grub_errno;

	*uuid = grub_env_get (BARA_UUID_ENV_VARIABLE);

	return grub_errno;
}

grub_err_t getClientMac(grub_efi_mac_t* client_mac)
{
	grub_errno = GRUB_ERR_NONE;

	if(!client_mac)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		grub_memset(client_mac, 0, sizeof(grub_efi_mac_t));

		grub_efi_loaded_image_t *image = grub_efi_get_loaded_image (grub_efi_image_handle);

		if (image)
		{
			grub_efi_simple_network_t *net = grub_efi_open_protocol (image->device_handle, &net_io_guid, GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

			if(net && net->mode)
			{
				grub_memcpy(client_mac, net->mode->current_address, sizeof(grub_efi_mac_t));
			}
			else
			{
				grub_error(GRUB_ERR_BAD_MODULE, "Cannot get mac address from efi_simple_network protocol!");
			}
		}
		else
		{
			grub_error(GRUB_ERR_BAD_MODULE, "Cannot open loaded image from image handle!");
		}
	}

	return grub_errno;
}

grub_err_t printEfiMacAddress(grub_efi_mac_t* mac_address)
{
	grub_errno = GRUB_ERR_NONE;

	if(NULL != mac_address)
	{
		grub_printf("%02x:%02x:%02x:%02x:%02x:%02x",
				((grub_uint8_t*)mac_address)[0], ((grub_uint8_t*)mac_address)[1], ((grub_uint8_t*)mac_address)[2],
				((grub_uint8_t*)mac_address)[3], ((grub_uint8_t*)mac_address)[4], ((grub_uint8_t*)mac_address)[5]);
	}
	else
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}

	return grub_errno;
}

grub_err_t convertEfiMacToString(grub_efi_mac_t* mac_address, char* mac_string)
{
	grub_errno = GRUB_ERR_NONE;

	if(mac_address && mac_string)
	{
		grub_snprintf(mac_string, 13, "%02x%02x%02x%02x%02x%02x",
				((grub_uint8_t*)mac_address)[0],((grub_uint8_t*)mac_address)[1], ((grub_uint8_t*)mac_address)[2],
				((grub_uint8_t*)mac_address)[3], ((grub_uint8_t*)mac_address)[4], ((grub_uint8_t*)mac_address)[5]);
	}
	else
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}

	return grub_errno;
}

grub_err_t getDhcpServerIp(grub_efi_pxe_ip_address_t* server_ip)
{
	grub_errno = GRUB_ERR_NONE;

	if(!server_ip)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		grub_memset(server_ip, 0, sizeof(grub_efi_pxe_ip_address_t));
		grub_efi_pxe_t *pxe = getEfiPxeBaseCodeProtocol();

		if(NULL != pxe &&  NULL != pxe->mode)
		{
			if(pxe->mode->pxe_reply_received)
			{
				struct grub_net_bootp_packet* pxe_reply = (struct grub_net_bootp_packet*)&pxe->mode->pxe_reply;
				grub_memcpy(&server_ip->addr, &pxe_reply->server_ip, 4);
			}
			else if(pxe->mode->proxy_offer_received)
			{
				struct grub_net_bootp_packet* proxy_offer = (struct grub_net_bootp_packet*)&pxe->mode->proxy_offer;
				grub_memcpy(&server_ip->addr, &proxy_offer->server_ip, 4);
			}
			else if(pxe->mode->dhcp_ack_received)
			{
				struct grub_net_bootp_packet* dhcp_ack = (struct grub_net_bootp_packet*)&pxe->mode->dhcp_ack;
				grub_memcpy(&server_ip->addr, &dhcp_ack->server_ip, 4);
			}
			else
			{
				grub_error(GRUB_ERR_BAD_MODULE, "no dhcp packet recieved!");
			}
		}
		else
		{
			grub_error(GRUB_ERR_BAD_MODULE, "cannot open pxe base code protocol!");
		}
	}

	return grub_errno;
}

grub_err_t printEfiIpv4Address(const grub_efi_pxe_ip_address_t* const ip_address)
{
	grub_errno = GRUB_ERR_NONE;

	if(ip_address)
	{
		grub_printf("%d.%d.%d.%d", ip_address->v4.addr[0], ip_address->v4.addr[1], ip_address->v4.addr[2], ip_address->v4.addr[3]);
	}
	else
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}

	return grub_errno;
}

grub_err_t getDhcpProcessorArchitectureType(grub_uint16_t* arch_type)
{
	grub_errno = GRUB_ERR_NONE;
	*arch_type = 0;

	if(0 >= parseDhcpDiscoverOptions(architectureOption, arch_type))
	{
		grub_error(GRUB_ERR_BAD_MODULE, "error detect processor architecture type!");
	}

	return grub_errno;
}

int dumpDhcpDiscoverOptions(void)
{
	return parseDhcpDiscoverOptions(dumpOption, NULL);
}


int parseDhcpDiscoverOptions(int (*option_handler)(const grub_uint8_t, const grub_uint8_t, const grub_uint8_t*, void*), void* outdata)
{
	int ret = -1;
	grub_efi_pxe_t *pxe = getEfiPxeBaseCodeProtocol();

	if(pxe)
	{
		const grub_efi_pxe_mode_t *pxe_mode = pxe->mode;

		// start position of the options section in the dhcp discover packet sizeof(grub_net_boot_packet) + 4 Bytes (DHCP Magic)
		unsigned int options_section = sizeof(struct grub_net_bootp_packet) + 4;

		// max size of the dhcp discover packet
		unsigned int max_packet_size = sizeof(grub_efi_pxe_packet_t);

		// option which marks the end of the options section in the dhcp discover packet
		const unsigned char option_end = 0xff;

		ret = 0;
		unsigned int pos;
		for(pos = 0; pos < max_packet_size - options_section && pxe_mode->dhcp_discover.raw[options_section + pos] != option_end; pos++)
		{
			// print the option number
			const grub_uint8_t option_number = pxe_mode->dhcp_discover.raw[options_section + pos];

			// read the length of the option data
			const grub_uint8_t option_data_length = (grub_uint8_t)pxe_mode->dhcp_discover.raw[options_section + (++pos)];

			// if the option handler returns 0 then we found the option
			if(0 == option_handler(option_number, option_data_length, &pxe_mode->dhcp_discover.raw[options_section + pos + 1], outdata))
			{
				ret++;
				break;
			}

			pos += option_data_length;
		}
	}

	return ret;
}

int dumpOption(const grub_uint8_t option_number, const grub_uint8_t option_data_length, const grub_uint8_t* data, void *unused)
{
	UNUSED(unused);

	// dump the data
	grub_printf("option %u: ", option_number);

	grub_uint8_t i;
	for (i = 0; i < option_data_length ; i++)
	{
		grub_printf("0x%02x ", (grub_uint8_t)data[i]);
	}
	grub_printf("\n");

	// we didn't found the searched option because we want to dump all options
	return -1;
}

int architectureOption(const grub_uint8_t option_number, const grub_uint8_t option_data_length, const grub_uint8_t* data, void* arch_type)
{
	int ret = -1;

	// option number for client system architecture
	const grub_uint8_t option_architecture = 93;

	if(option_architecture == option_number && option_data_length == 2)
	{
		*(grub_uint16_t*)arch_type = b_ntohs(*(grub_uint16_t*)data);

		if (isdebug())
		{
			grub_printf("processor architecture type (option %u, length %u, value %u)\n ", option_number, option_data_length, *(grub_uint16_t*)arch_type);
		}

		ret = 0;
	}

	// if we didn't find the option we return -1
	return ret;
}

grub_uint8_t b_ntohs(grub_uint16_t x)
{
	return ((((x) & 0xff) << 8) | (((x) & 0xff00) >> 8));
}

/* Remove leading and trailing whitespace */
void trim(char* const s)
{
	char* p = s;
	char* q = s;

	if (!*s)
    return;

	/* Find first non whitespace character */
	while (grub_isspace(*p))
		p++;

	if (s != p)
	/* Copy content to beginning of s if there where leading white space characters */
		while (*p)
			*q++ = *p++;
	else
    /* else find end of string */
		while (*q)
		  q++;

	/* Find last non whitespace character */
	if (s != q)
		while (grub_isspace(*(q-1)))
			q--;

	/* Terminate string after the last non whitespace character */
	*q = '\0';
}

int getkey(const unsigned long timeoutMilliSeconds)
{
	int key = GRUB_TERM_NO_KEY;
	unsigned long count = 0;

	/* If a timeout value is provided, wait till somebody hit a key or the timeout is expired */
	if (0 != timeoutMilliSeconds)
	{
		grub_refresh ();
		key = grub_getkey_noblock ();

		while (GRUB_TERM_NO_KEY == key && (count++ < timeoutMilliSeconds/10))
		{
			grub_millisleep(10);
			key = grub_getkey_noblock ();
		}
	}
	else
	{
		key = grub_getkey();
	}

	return key;
}

int waitforkey(const int key, const unsigned long delay_seconds)
{
	int readKey = GRUB_TERM_NO_KEY;
	unsigned long count = 0;

	/* If a timeout value is provided, wait the delay or until the desired key is pressed */
	if (0 != delay_seconds)
	{
		while ((count++ < delay_seconds/10) && (key != readKey))
		{
			grub_millisleep(10);
			readKey = grub_getkey_noblock ();
		}
	}

	return readKey;
}

int grub_quit()
{
   	grub_cls();
    grub_efi_system_table->boot_services->exit (grub_efi_image_handle, GRUB_EFI_NOT_STARTED, 0, 0);
    for (;;);
    return 0;
}

grub_err_t chainload_uefi_winpe(const char* new_bootfilename, const grub_efi_pxe_ip_address_t* const server_ip, const grub_efi_boolean_t always_use_secureboot_chainload)
{
	grub_errno = GRUB_ERR_NONE;

	if(!new_bootfilename || !server_ip)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else if(GRUB_ERR_NONE == changeDhcpAckParms(new_bootfilename, server_ip))
	{
		// copy bootfilename because it's const
		char cmd[130];
		grub_memset(cmd, 0, 130);

		if(0 == grub_strncmp(new_bootfilename, "/", 1))
		{
			grub_strncpy(cmd, new_bootfilename, 128);
		}
		else if (0 == grub_strncmp(new_bootfilename, "(pxe)/", 6))
		{
			grub_strncpy(cmd, new_bootfilename+5, 123);
		}
		else
		{
			grub_snprintf(cmd, 130, "/%s", new_bootfilename);
		}

		if(GRUB_ERR_NONE != pxechainloader(cmd, server_ip, always_use_secureboot_chainload))
		{
			grub_printf("\nWindows PE network boot [%s] failed: \n", cmd);

			debugdelay(5);
		}
		else
		{
			grub_command_execute("boot", 0, 0);
		}
	}

	return grub_errno;
}

// seed the pseudo-random number generator
void srandom (unsigned int seed)
{
	rnd_seed = seed;
}

// generate a pseudo-random number between 0 and 2147483647L or 2147483562?
long int random (void)
{
	grub_int32_t q;

	if (!rnd_seed) // Initialize linear congruential generator
		srandom(grub_get_time_ms());
	else
		srandom(rnd_seed * grub_get_time_ms());

	// simplified version of the LCG given in Bruce Schneier's "Applied Cryptography"
	q = (rnd_seed / 53668);
	rnd_seed = (40014 * (rnd_seed - 53668 * q) - 12211 * q);

	if (rnd_seed < 0)
		rnd_seed += 2147483563L;

	return rnd_seed;
}

int mac_to_int(grub_efi_mac_t* mac_address)
{
	int i = 0,
		mac_as_int = 0;

	// size of a mac in bytes is 6
	for (i = 0; i < (int)sizeof(int) && i < 6; i++)
	{
		mac_as_int |= (((grub_uint8_t*)mac_address)[i] << (i*8));
	}

	return mac_as_int;
}

// Grub handles its own environment variables
// This function changes the boot-server within the grub variables.
//void switchGrubEnvironment(const char* server, const int bootFromMenuServer)
//{
//	if (bootFromMenuServer > 0)
//	{
//		if (0 == grub_strncasecmp(server, "(tftp", 5))
//		{
//			int len = grub_strlen(server);
//
//			if (len <= MAX_PROPERTYVALUELENGTH)
//			{
//				char c = server[len - 1];
//				if (c == ')')
//				{
//					grub_env_set("root", server);
//				}
//			}
//		}
//		else if (NULL != server && 0 != grub_strcmp(server, ""))
//		{
//			char* rootDirectory = NULL;
//			rootDirectory = grub_xasprintf("(tftp,%s)", server);
//			grub_env_set("root", rootDirectory);
//			grub_free(rootDirectory);
//			rootDirectory = NULL;
//		}
//	}
//	else
//	{
//		if (isdebug())
//		{
//			grub_printf("Flag bootfrommenuserver not set --> Using BootServer %s \n", grub_env_get("root"));
//		}
//	}
//}
