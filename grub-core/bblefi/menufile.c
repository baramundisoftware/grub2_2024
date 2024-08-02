//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#include <grub/term.h>
#include <grub/dl.h>
#include "menufile.h"
#include "bms_actions.h"
#include "bblefi_config.h"
#include "utils.h"

#define MAX_S_LENGTH 40

GRUB_MOD_LICENSE("GPLv3+");

/*******************************************************************************************************************/

grub_size_t my_fread(void* buffer, grub_size_t size , grub_size_t count , char** const actualbuffer);
int read_string(char** const actualbuffer, grub_size_t reclen, char* const str, grub_size_t maxlen);
int int_from_hex(const char* const hex, const int max, int* const value);
int calculate_random_delay(int delay, const int max_delay, const int random_seed, grub_efi_boolean_t* srand_called);

/*******************************************************************************************************************/

int int_from_hex(const char* const hex, const int max, int* const value)
{
	int success = 1;
	int count = max;
	const char* p = hex;

	*value = 0;
	while (*p && count-- && success)
	{
		*value *= 16;
		if (*p >= '0' && *p <= '9')
		{
			*value += *p - '0';
		}
		else
		{
			if (*p >= 'a' && *p <= 'f')
			{
				*value += *p - 'a' + 10;
			}
			else
			{
				if (*p >= 'A' && *p <= 'F')
				{
					*value += *p - 'A' + 10;
				}
				else
				{
					success = 0;
				}
			}
		}
		p++;
	}
	return success;
}

grub_size_t my_fread(void* buffer, grub_size_t size , grub_size_t count , char** const current_pos)
{
	grub_size_t  read_bytes = 0;
	char s[MAX_S_LENGTH + 1];
	grub_memset(s, 0, MAX_S_LENGTH + 1);

//	if (0 != isdebug())
//		grub_printf("-> grub_file_read(%p, %u, %p)\n", buffer, (unsigned int)(size*count), stream);

	grub_size_t buffer_length = grub_strlen(*current_pos);
	read_bytes = (buffer_length <= (count*size)) ? buffer_length : (count*size);

	if(read_bytes > 0)
	{
		grub_strncpy(buffer, (const char*)(*current_pos), read_bytes);
		(*current_pos) += read_bytes;
	}

//	if (0 != isdebug())
//	{
//		grub_strncpy(s, (const char*)buffer, read_bytes > MAX_S_LENGTH ? MAX_S_LENGTH : read_bytes);
//		trim(s);
//		grub_printf("<- grub_file_read(\"%s\"), read_bytes %d\n", s, (int)read_bytes);
//	}

	return read_bytes;
}

int read_string(char** const current_pos, grub_size_t request_length, char* const str, grub_size_t max_length)
{
	int success = 1;
	grub_size_t length_to_read = (request_length > max_length) ? max_length : request_length;
	grub_size_t buffer_length = grub_strlen(*current_pos);

	// check if the bytes to read are in the buffer or the buffer is smaller
	length_to_read = (buffer_length < length_to_read) ? buffer_length : length_to_read;

	grub_size_t reminder = request_length - length_to_read;

	success &= (my_fread(str, 1, length_to_read, current_pos) == length_to_read);
	if (reminder > 0)
	{
		*current_pos += reminder;
	}

	return success;
}

int calculate_random_delay(int delay, const int max_delay, const int random_seed, grub_efi_boolean_t* srand_called)
{
	if (0 != max_delay && 0 != random_seed)
	{
		if (srand_called && !(*srand_called))
		{
			srandom(random_seed);
			*srand_called = TRUE;
		}

		/* COM32 defines RAND_MAX as 0x7fffffff but uses all bits... */
		int random_number = random();

		/* We are not supposed to use the low-order bits (see man 3 rand on Ubuntu)
		   but this seems to deliver better numbers, so do it anyway... */
		delay = 1 + (random_number % (max_delay * 1000));

		if (delay < 1000)
			delay = 1000;
		else if (delay > max_delay * 1000)
			delay = max_delay * 1000;
	}
	else
	{
		delay = 10 * 1000;
	}

	return delay;
}

/**

  \return Number of entries read from the menu file, 0 = server doesn't want to serve this request (e.g. ignore
  requests from unknown clients), -1 = error contacting server.

*/
grub_err_t LoadBootLoaderMenuFile(grub_efi_pxe_ip_address_t* server_ip, grub_efi_mac_t* client_mac, const grub_uint16_t arch_type,
							BootLoaderMenuEntryT* const entries, const int max_entries, const int retries,
							const int max_delay)
{
	grub_errno = GRUB_ERR_NONE;

	if(!server_ip || !client_mac || !entries)
	{
			grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		/* Set random seed */
		int random_seed = mac_to_int(client_mac);

		grub_efi_boolean_t srand_called = FALSE;
		int index = 0;

		if (retries > 0)
		{
			grub_printf("Retries: %d, Max delay between retries: %d seconds\n", retries, max_delay);

			if (isdebug())
				grub_printf("Random seed: %08x", random_seed);
		}

		char* menu_file_buffer = NULL;

		int retryCount = retries;

		if (GRUB_ERR_NONE != loaddynamicbootmenu(server_ip, client_mac, arch_type, &menu_file_buffer) && retryCount > 0)
		{
			while (retryCount > 0 && !menu_file_buffer)
			{
				/* If the menu file was not found, this typically indicates a server */
				/* overload. So retry later after a random delay. */
				if (!menu_file_buffer)
				{
					int delay = 10; // default delay will be 10 seconds
					delay = calculate_random_delay(delay, max_delay, random_seed, &srand_called);

					grub_printf("Did not get menu, retrying in %d.%03d seconds (Press ESC to abort)...\n", delay / 1000, delay % 1000);

					if (GRUB_TERM_ESC == waitforkey(GRUB_TERM_ESC, delay))
					{
						grub_printf("\nEsc Pressed. Aborting retry...");
						retryCount = 0;
						break;
					}
				}

				if (GRUB_ERR_NONE == loaddynamicbootmenu(server_ip, client_mac, arch_type, &menu_file_buffer))
				{
					break;
				}

				retryCount--;
			}
		}

		if(GRUB_ERR_NONE == grub_errno)
		{
			grub_efi_boolean_t success = TRUE;
			int feof = 0;

			grub_size_t buffersize = grub_strlen(menu_file_buffer);
			char* actualposition = menu_file_buffer;

			while (!(actualposition >= (menu_file_buffer + buffersize))
					&& FALSE != success
					&& (index < max_entries))
			{
				char hex[8];
				success &= ((int)my_fread(hex, 1, 2, &actualposition) == 2);

				if (FALSE == success)
				{
					// if we are not able to read the start of a record, we assume eof
					feof = 1;
					success = TRUE;
				}
				else
				{
					BootLoaderMenuEntryT entry;
					grub_memset(&entry, 0, sizeof(entry));

					int tmp = 0;

					// Type
					success &= int_from_hex(hex, 2, &tmp);
					entry.m_eType = (EBootLoaderMenuEntryType)tmp;

					// Flags
					success &= ((int)my_fread(hex, 1, 8, &actualposition) == 8);
					success &= int_from_hex(hex, 8, &tmp);
					entry.m_eFlags = (EBootLoaderMenuEntryFlags)tmp;

					// Text
					success &= ((int)my_fread(hex, 1, 4, &actualposition) == 4);
					success &= int_from_hex(hex, 4, &tmp);
					success &= read_string(&actualposition, tmp, entry.m_szText, MAX_BOOTLOADERMENUENTRYTEXTLEN);

					// Command
					success &= ((int)my_fread(hex, 1, 4, &actualposition) == 4);
					success &= int_from_hex(hex, 4, &tmp);
					success &= read_string(&actualposition, tmp, entry.m_szCommand, MAX_BOOTLOADERMENUENTRYCOMMANDLEN);

					// BootEnvGUID
					success &= ((int)my_fread(hex, 1, 4, &actualposition) == 4);
					success &= int_from_hex(hex, 4, &tmp);
					success &= read_string(&actualposition, tmp, entry.m_szBootEnvGUID, MAX_BOOTLOADERMENUENTRYGUIDLEN);

					// TFTPPathPrefix
					success &= ((int)my_fread(hex, 1, 4, &actualposition) == 4);
					success &= int_from_hex(hex, 4, &tmp);
					success &= read_string(&actualposition, tmp, entry.m_szTFTPPathPrefix, MAX_BOOTLOADERMENUENTRYPATHPREFIX);

					// Trailing newline
					success &= ((int)my_fread(hex, 1, 1, &actualposition) == 1);


					if (success)
					{
						grub_memcpy(&entries[index++], &entry, sizeof(entry));
					}
					if (isdebug())
					{
						grub_printf("type:%d flags:%d text:%s command:%s bootenv:%s TftpPathPrefix:%s\n",
								entry.m_eType, entry.m_eFlags,entry.m_szText, entry.m_szCommand, entry.m_szBootEnvGUID, entry.m_szTFTPPathPrefix);
					}
				}
			}

			if (FALSE == success)
			{
				grub_error(GRUB_ERR_BAD_MODULE, "Parsing menu file failed!");
			}
			else if (max_entries == index && !feof)
			{
				grub_error(GRUB_ERR_BAD_MODULE, "More than the max. allowed [%d] menu entries received!", max_entries);
			}

			if(menu_file_buffer)
			{
				grub_free(menu_file_buffer);
				menu_file_buffer = NULL;
			}
		}

		debugdelay(5);
	}

	return grub_errno;
}
