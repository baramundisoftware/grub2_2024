//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/env.h>
#include <grub/term.h>
#include <grub/dl.h>
#include "utils.h"
#include "bblefi_config.h"
#include "menu.h"
#include "menufile.h"

GRUB_MOD_LICENSE("GPLv3+");

/*******************************************************************************************************************/

void header(void);
grub_efi_boolean_t f8prompt(const char* const servername, const int timeout);

grub_err_t loadmenu(grub_efi_pxe_ip_address_t* server_ip, grub_efi_mac_t* client_mac, const grub_uint16_t arch_type,
			const int menuRetries, BootLoaderMenuEntryT* const entries, const int maxEntries,
			int* const mapChoiceToEntry, unsigned* const timeout, char* const servername,
			const int maxServernameLength, int* const choice, int* const noOfEntries);

grub_err_t dynamicmenu(BootLoaderMenuEntryT* const entries, const int maxEntries, const int* const mapChoiceToEntry, int* const choice);

const char* const g_appname = "baramundi UEFI PXE Boot Loader";
const char* const g_version = __DATE__ " " __TIME__;

/*******************************************************************************************************************/

void header()
{
	// go to top
	struct grub_term_coordinate pos;
	pos.x = 0;
	pos.y = 0;
	grub_term_restore_pos (&pos);

	// print header
	grub_env_set ("color_normal", "white/blue");
	grub_printf("\n %s \n", g_appname);

	grub_env_set ("color_normal", "white/black");
	grub_printf(" Version %s (GRUB %s)\n\n", g_version, PACKAGE_VERSION);
}

grub_efi_boolean_t f8prompt(const char* const servername, const int timeout)
{
	grub_cls();
	header();

	grub_printf(" baramundi PXE BootClient [Server: %s] ", servername);
	struct grub_term_coordinate* pos = grub_term_save_pos();

	grub_efi_boolean_t showmenu = FALSE;
	int key = GRUB_TERM_NO_KEY,
		count = timeout;
	do
	{
		grub_term_restore_pos (pos);
		grub_printf("(%d) ", count);

		key = getkey(1000);
	} while ((GRUB_TERM_NO_KEY == key) && --count);

	if (GRUB_TERM_KEY_F8 == key)
	{
		/* F8 key pressed */
		showmenu = TRUE;
	}

	grub_term_restore_pos (pos);
	grub_printf("     ");

	return showmenu;
}

grub_err_t loadmenu(grub_efi_pxe_ip_address_t* server_ip, grub_efi_mac_t* client_mac, const grub_uint16_t arch_type,
			const int menuRetries, BootLoaderMenuEntryT* const entries, const int maxEntries,
			int* const mapChoiceToEntry, unsigned* const timeout, char* const servername,
			const int maxServernameLength, int* const choice, int* const noOfEntries)
{
	grub_errno = GRUB_ERR_NONE;

	if(!server_ip || !client_mac || !entries || !timeout || !servername || !choice || !noOfEntries)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		// reset number of menu entries
		*noOfEntries = 0;

		grub_printf("Trying to load menu file\n");
		if (GRUB_ERR_NONE == LoadBootLoaderMenuFile(server_ip, client_mac, arch_type, entries, maxEntries, menuRetries, 20))
		{
			int i = 0;
			/* Find default entry and store it as current choice */
			for (i = 0; i < maxEntries; i++)
			{
				switch (entries[i].m_eType)
				{
					case eBLMETTimeout:
						if (NULL != timeout)
						{
							*timeout = grub_strtoul(entries[i].m_szCommand, NULL, 10);
						}
						break;
					case eBLMETServerName:
						grub_strncpy(servername, entries[i].m_szCommand, maxServernameLength);
						break;
					case eBLMETSyslinuxLocalHDD:
					/* case eBLMETSyslinuxPrompt:
					case eBLMETSyslinuxCommand:
					case eBLMETSyslinuxChainload: */
					case eBLMETGrubWinPEChainload:
					case eBLMETGrubPrompt:
						mapChoiceToEntry[(*noOfEntries)++] = i;
						if ((entries[i].m_eFlags & eBLMEFDefault) && (NULL != choice))
							*choice = *noOfEntries;
						break;
				  default:
					  break;
				}
			}

			if (0 == *noOfEntries)
			{
				grub_printf("%s doesn't care about us.\n", servername);
			}
		}
	}

	return grub_errno;
}

grub_err_t dynamicmenu(BootLoaderMenuEntryT* const entries, const int maxEntries, const int* const mapChoiceToEntry, int* const choice)
{
	if(!entries || !mapChoiceToEntry || !choice)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		int i = 0,
			noOfEntries = 0;

		for (i = 0; i < maxEntries; i++)
		{
			if (choice && (i == mapChoiceToEntry[(*choice)-1]))
			{
				grub_env_set ("color_normal", "black/green");
			}
			else
			{
				grub_env_set("color_normal", "white/black");
			}

			switch (entries[i].m_eType)
			{
				case eBLMETSyslinuxLocalHDD:
				/* case eBLMETSyslinuxPrompt:
				case eBLMETSyslinuxCommand:
				case eBLMETSyslinuxChainload: */
				case eBLMETGrubWinPEChainload:
				case eBLMETGrubPrompt:
					grub_printf(" %2d: ", ++noOfEntries);
					break;
				default:
					break;
			}
			switch (entries[i].m_eType)
			{
				case eBLMETText:
				case eBLMETSyslinuxLocalHDD:
				/* case eBLMETSyslinuxPrompt:
				case eBLMETSyslinuxCommand:
				case eBLMETSyslinuxChainload: */
				case eBLMETGrubWinPEChainload:
				case eBLMETGrubPrompt:
					grub_printf("%s", entries[i].m_szText);
					break;
				default:
					break;
			}
			switch (entries[i].m_eType)
			{
				case eBLMETSyslinuxLocalHDD:
				/* case eBLMETSyslinuxPrompt:
				case eBLMETSyslinuxCommand:
				case eBLMETSyslinuxChainload: */
				case eBLMETGrubWinPEChainload:
				case eBLMETGrubPrompt:
					grub_printf(" \n");
					break;
				default:
					break;
			}

			/* Reset attributes to "normal" */
			grub_env_set("color_normal", "white/black");
		}
	}

	return grub_errno;
}

grub_err_t menu(grub_efi_pxe_ip_address_t* server_ip,
		grub_efi_mac_t* client_mac,
		const grub_uint16_t arch_type,
		const int menuRetries,
		const BootLoaderMenuEntryT** chosenEntry,
		int* const defaultchosen,
		int* const choice)
{
	grub_errno = GRUB_ERR_NONE;

	if(!server_ip || !client_mac || !chosenEntry || !defaultchosen || !choice)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else if(! server_ip->v4.addr[0] && ! server_ip->v4.addr[1] && ! server_ip->v4.addr[2] && ! server_ip->v4.addr[3])
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "server ip not valid!");
	}
	else
	{
		// set parameters to default return values
		*chosenEntry = NULL;
		*choice = -1;

		static BootLoaderMenuEntryT entries[MENU_ENTRIES_MAX];
		grub_memset(entries, 0, MENU_ENTRIES_MAX * sizeof(BootLoaderMenuEntryT));

		int mapChoiceToEntry[20];
		grub_memset(mapChoiceToEntry, 0, 20 * sizeof(int));

		char servername[SERVERNMAE_LENGTH_MAX+1];
		grub_memset(entries, 0, SERVERNMAE_LENGTH_MAX+1);

		int defaultchoice = -1,
		    noOfEntries = 0;

		unsigned timeout = 5;

		if(GRUB_ERR_NONE == loadmenu(server_ip, client_mac, arch_type, menuRetries, entries, MENU_ENTRIES_MAX, mapChoiceToEntry, &timeout, servername, SERVERNMAE_LENGTH_MAX, &defaultchoice, &noOfEntries))
		{
			*choice = defaultchoice;

			if (noOfEntries > 0)
			{
				if (FALSE == f8prompt(servername, timeout))
				{
					/* Set default entry as nobody dared to press F8 */
					*chosenEntry = &entries[mapChoiceToEntry[(*choice) - 1]];
				}
				else
				{
					/* F8 pressed, present menu */
					grub_cls();

					int key = '\0';
					do
					{
						/* Menu header */
						header();

						/* Menu entries, including "normal" text, selectable entries and configuration values (e.g. timeout) */
						dynamicmenu(entries, MENU_ENTRIES_MAX, mapChoiceToEntry, choice);
						timeout = 0; /* Disable timeout in this menu, as it is needed earlier (F8) */

						if (noOfEntries > 0)
						{
							/* Safe cursor position for later output of remaining timeout */
							struct grub_term_coordinate* pos = grub_term_save_pos();

							/* If a timeout is still active, call getkey() in a loop and update shown timeout value every second */
							if (timeout > 0)
							{
								do
								{
									grub_term_restore_pos (pos);
									grub_printf("(%d) ", timeout);

									key = getkey(1000);
								} while ((GRUB_TERM_NO_KEY == key) && --timeout);

								if (GRUB_TERM_NO_KEY == key)
								{
									/* No key pressed */
									*chosenEntry = &entries[mapChoiceToEntry[(*choice) -1]];
								}

								timeout = 0;
								grub_term_restore_pos (pos);
								grub_printf("     ");
							}

							/* No timeout is active, just wait for a key pressed */
							else
							{
								key = getkey(0);
							}

							/* Chosen by entry number */
							if (key >= '1' && key <= noOfEntries + '0')
							{
								*choice = key - '0';
								*chosenEntry = &entries[mapChoiceToEntry[(*choice) -1]];
							}

							/* Cursor up */
							if (GRUB_TERM_KEY_UP == key)
							{
								(*choice)--;
								if ((*choice) < 1)
									(*choice) = noOfEntries;
							}

							/* Cursor down */
							if (GRUB_TERM_KEY_DOWN == key)
							{
								(*choice)++;
								if ((*choice) > noOfEntries)
									(*choice) = 1;
							}

							/* Enter */
							if ('\r' == key)
							{
								*chosenEntry = &entries[mapChoiceToEntry[(*choice) -1]];
							}
						}
					} while (NULL == *chosenEntry);

					grub_printf("\n");
				}
			}
		}

		if (NULL != defaultchosen)
		{
			*defaultchosen = (defaultchoice == (*choice));
		}
	}

	return grub_errno;
}
