//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#include <grub/err.h>
#include <grub/dl.h>
#include <grub/extcmd.h>
#include <grub/term.h>
#include <grub/time.h>
#include "utils.h"
#include "bms_actions.h"
#include "bblefi_config.h"
#include "menu.h"
#include "menufile.h"
#include "bara_efi_defines.h"

#define MAX_SERVERS 8

GRUB_MOD_LICENSE("GPLv3+");

/*******************************************************************************************************************/

grub_err_t StartProcessing(void);

grub_err_t showMenu(grub_efi_pxe_ip_address_t* menu_server_ip,
					grub_efi_pxe_ip_address_t* tftp_server_ip,
					const char* menu_servers[],
					grub_efi_mac_t* client_mac,
					const grub_uint16_t arch_type,
					const int menu_retries,
					const BootLoaderMenuEntryT** chosenEntry,
					int* defaultchosen,
					int noOfServers);

grub_err_t processChosenMenuEntry(const grub_efi_pxe_ip_address_t* server_ip,
								  grub_efi_mac_t* client_mac,
								  const BootLoaderMenuEntryT* const chosenEntry,
								  const int defaultchosen,
								  const grub_efi_boolean_t dont_set_pathprefix,
								  const grub_efi_boolean_t always_use_secureboot_chainload);

/*******************************************************************************************************************/

static grub_err_t grub_cmd_bblefi(
		grub_extcmd_context_t ctxt __attribute__ ((unused)),
		int argc __attribute__ ((unused)), char **args __attribute__ ((unused))) {

	// if there is an error then we want to see it
	grub_err_t error_code;
	if (GRUB_ERR_NONE != (error_code = StartProcessing()))
	{
		if (GRUB_ERR_BAD_SIGNATURE == error_code)
		{
			grub_print_error();
			getkey(30 * 1000);
		}
		else
		{
			grub_print_error();
			debugdelay(10);
		}
	}

	// always quit grub to local boot
	grub_quit();
	return grub_errno;
}


grub_err_t showMenu(grub_efi_pxe_ip_address_t* menu_server_ip,
					grub_efi_pxe_ip_address_t* tftp_server_ip,
					const char* menu_servers[],
					grub_efi_mac_t* client_mac,
					const grub_uint16_t arch_type,
					const int menu_retries,
					const BootLoaderMenuEntryT** chosenEntry,
					int* defaultchosen,
					int noOfServers)
{
	grub_errno = GRUB_ERR_NONE;

	if(!menu_server_ip || !tftp_server_ip || !client_mac || !menu_servers)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		// set defaults
		*chosenEntry = NULL;
		grub_memset(menu_server_ip, 0, sizeof(grub_efi_pxe_ip_address_t));
		*defaultchosen = 0;

		int choice = -1,
			serverNo = 0;

		do
		{
			grub_memset(menu_server_ip, 0, sizeof(grub_efi_pxe_ip_address_t));

			if(grub_strcmp(menu_servers[serverNo],"") == 0)
			{
				*menu_server_ip = *tftp_server_ip;
			}
			else
			{
				if (GRUB_ERR_NONE != dnsRequestServerIp(menu_servers[serverNo], menu_server_ip) && isdebug())
				{
					// If startserver is defined by a FQDN and a namespace lookup cannot be performed
					// the client should boot from the local drive.
					grub_print_error();
					grub_errno = GRUB_ERR_NONE;
				}
			}

			if(menu_server_ip->addr)
			{
				grub_printf("server ip [server number %d]: ", serverNo);
				printEfiIpv4Address(menu_server_ip);
				grub_printf("\n");

				if(GRUB_ERR_NONE != menu(menu_server_ip, client_mac, arch_type, menu_retries, chosenEntry, defaultchosen, &choice)
					&& isdebug())
				{
					grub_print_error();
					grub_errno = GRUB_ERR_NONE;
				}
			}
			else if(isdebug())
			{
				grub_printf("server ip [server number %d]: ", serverNo);
				printEfiIpv4Address(menu_server_ip);
				grub_printf(" is not valid\n");
			}

			if (-1 == choice)
			{
				serverNo++;
			}
		} while ((-1 == choice) && (serverNo < noOfServers) && (serverNo < MAX_SERVERS));

		// reset error code, so we ignore all errors until now
		grub_errno = GRUB_ERR_NONE;
		// boot local disk if no server offers a boot menu
		if (serverNo >= MAX_SERVERS)
		{
			// initiate local boot
			grub_error(GRUB_ERR_MENU, "No boot server offered a boot menu, booting local disk\n");
			grub_print_error();
		}
	}

	return grub_errno;
}

grub_err_t processChosenMenuEntry(const grub_efi_pxe_ip_address_t* boot_server_ip,
								  grub_efi_mac_t* client_mac,
								  const BootLoaderMenuEntryT* const chosenEntry,
								  const int defaultchosen,
								  const grub_efi_boolean_t dont_set_pathprefix,
								  const grub_efi_boolean_t always_use_secureboot_chainload)
{
	grub_errno = GRUB_ERR_NONE;

	if(!boot_server_ip || !client_mac || !chosenEntry)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		grub_printf("\n\n%s\n\n", chosenEntry->m_szText);

		/* Update client boot environment */
		if ((chosenEntry->m_eFlags & eBLMEFUpdateBootEnv) && !defaultchosen)
		{
			grub_printf("Updating client boot environment...\n");
			if (GRUB_ERR_NONE != updateclientbootenv(boot_server_ip, client_mac, chosenEntry->m_szBootEnvGUID))
			{
				grub_print_error();
				// ignore this error
				grub_errno = GRUB_ERR_NONE;
			}
		}

		/* Set TFTP path prefix, but not for local boot */
		if ((chosenEntry->m_eType != eBLMETSyslinuxLocalHDD) && (chosenEntry->m_eType != eBLMETGrubPrompt) && FALSE == dont_set_pathprefix)
		{
			grub_printf("Setting TFTP path prefix to \"%s\"...\n", chosenEntry->m_szTFTPPathPrefix);

			// If the boot_from_menu_server-Option is set, the pathprefix should be set on the server which has delivered the boot-menu.
			if (GRUB_ERR_NONE != setpathprefix(boot_server_ip, client_mac, chosenEntry->m_szBootEnvGUID, chosenEntry->m_szTFTPPathPrefix))
			{
				grub_print_error();
				// ignore this error
				grub_errno = GRUB_ERR_NONE;
			}
		}
		else
		{
			grub_printf("Skipped setting the TFTP path prefix due to configuration\n");
		}

		debugdelay(5);

		/* Take chosen action */
		switch (chosenEntry->m_eType) {
		case eBLMETSyslinuxLocalHDD:
			grub_printf("Executing local boot\n");
			break;
		case eBLMETGrubPrompt:
			grub_printf("Executing grub prompt\n");
			// Todo: Check if the following lines can be removed.
			char cmnd[7];
			grub_memset(cmnd, 0, 7 * sizeof(char));
			grub_strncpy(cmnd, "normal", 7);
			char* command[1] = { cmnd };
			grub_command_execute("normal", 1, command);
			break;
		case eBLMETGrubWinPEChainload:
			grub_printf("PXE chainloading %s\n", chosenEntry->m_szCommand);
			// sets the grub_errno
			grub_errno = chainload_uefi_winpe(chosenEntry->m_szCommand, boot_server_ip, always_use_secureboot_chainload);
			break;
		default:
			break;
		}
	}

	return grub_errno;
}

grub_err_t StartProcessing(void)
{
	grub_errno = GRUB_ERR_NONE;

	grub_efi_pxe_ip_address_t tftp_server_ip;
	grub_memset(&tftp_server_ip, 0, sizeof(grub_efi_pxe_ip_address_t));
	if(GRUB_ERR_NONE != getDhcpServerIp(&tftp_server_ip))
	{
		grub_print_error();
		grub_sleep(5);
		return grub_errno;
	}
	else if (isdebug())
	{
		// show server address
		grub_printf("DHCP packet server IP: ");
		printEfiIpv4Address(&tftp_server_ip);
		grub_printf("\n");
	}

	const char* menu_servers[MAX_SERVERS];
	grub_memset(menu_servers, 0, MAX_SERVERS * sizeof(char*));

	const char* properties[MAX_PROPERTIES];
	grub_memset(properties, 0, MAX_PROPERTIES * sizeof(char*));

	const char* values[MAX_PROPERTIES];
	grub_memset(values, 0, MAX_PROPERTIES * sizeof(char*));

	int noOfServers = 0;

	if (GRUB_ERR_NONE != read_bblefi_config(&tftp_server_ip, menu_servers, properties, values, &noOfServers))
	{
		grub_print_error();
		grub_sleep(5);
		return grub_errno;
	}

	if (noOfServers < 1)
	{
		/* We were not able to read list of servers, print error message and
		default to local server (which provided the boot loader download) */
		grub_print_error();
		noOfServers = 1;
		menu_servers[0] = "";
	}


	/* Pull number of retries for reading menu file from bbl.cfg */
	int menu_retries = 0;
	const char* value = valueofproperty(properties, values, "menuretries");
	if (NULL != value)
	{
		menu_retries = (int)grub_strtoul(value, NULL, 10);
	}

	/* should the boot process load all files from the menu server */
	grub_efi_boolean_t boot_from_menu_server = FALSE;
	boot_from_menu_server = (0 == grub_strcasecmp("1", valueofproperty(properties, values, "bootfrommenuserver")));

	/* should the tftp sever not set the path prefix*/
	grub_efi_boolean_t dont_set_pathprefix = FALSE;
	dont_set_pathprefix = (0 == grub_strcasecmp("1", valueofproperty(properties, values, "dontsetpathprefix")));

	/* should the tftp sever not set the path prefix*/
	grub_efi_boolean_t always_use_secureboot_chainload = FALSE;
	always_use_secureboot_chainload = (0 == grub_strcasecmp("1", valueofproperty(properties, values, "alwayssecbootchain")));

	// get the mac address of the client
	grub_efi_mac_t client_mac;
	grub_memset(&client_mac, 0, sizeof(grub_efi_mac_address_t));
	if(GRUB_ERR_NONE != getClientMac(&client_mac))
	{
		return grub_errno;
	}
	else if (isdebug())
	{
		grub_printf("MAC address of client: ");
		printEfiMacAddress(&client_mac);
		grub_printf("\n");
	}

	// get firmware type from dhcp discover packet
	grub_uint16_t arch_type = 0;
	if (GRUB_ERR_NONE != getDhcpProcessorArchitectureType(&arch_type))
	{
		return grub_errno;
	}
	else if (isdebug())
	{
		grub_printf("Processor architecture type of client: %u\n", arch_type);
	}

	debugdelay(5);

	// show menu and process selection
	const BootLoaderMenuEntryT* chosenEntry = NULL;
	int defaultchosen = 0;
	grub_efi_pxe_ip_address_t menu_server_ip;
	grub_memset(&menu_server_ip, 0, sizeof(grub_efi_pxe_ip_address_t));
	if(GRUB_ERR_NONE == showMenu(&menu_server_ip, &tftp_server_ip, menu_servers, &client_mac, arch_type, menu_retries, &chosenEntry, &defaultchosen, noOfServers))
	{
		grub_efi_pxe_ip_address_t boot_server_ip = (FALSE != boot_from_menu_server) ? menu_server_ip : tftp_server_ip;
		// sets grub_errno inside
		processChosenMenuEntry(&boot_server_ip, &client_mac, chosenEntry, defaultchosen, dont_set_pathprefix, always_use_secureboot_chainload);
	}

	return grub_errno;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(bblefi) {
	cmd = grub_register_extcmd("bblefi", grub_cmd_bblefi, 0, 0,
			N_("baramundi UEFI bootloader"), 0);
}

GRUB_MOD_FINI(bblefi) {
	grub_unregister_extcmd(cmd);
}

