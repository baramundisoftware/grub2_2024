//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include "bms_actions.h"
#include "bblefi_config.h"
#include "utils.h"

#define FILENAME_MAX 128
#define LINELENGTH_MAX 1024
#define REQUESTLENGTH_MAX 128
#define ACTION_MAX 128

GRUB_MOD_LICENSE("GPLv3+");

/*******************************************************************************************************************/

grub_err_t genericserverrequest(const grub_efi_pxe_ip_address_t* server_ip, grub_efi_mac_t* client_mac, const char* const action);
grub_err_t genericbmsfilerequest(const grub_efi_pxe_ip_address_t* server_ip, grub_efi_mac_t* client_mac, const char* const action, char** buffer);
grub_err_t generatefilename(char* filename, grub_efi_mac_t* client_mac, const char* uuid, const char* action);

/*******************************************************************************************************************/

grub_err_t updateclientbootenv(const grub_efi_pxe_ip_address_t* const serverip, grub_efi_mac_t* client_mac, const char* const bootEnvGUID)
{
	grub_errno = GRUB_ERR_NONE;

	if(!client_mac || !serverip || !bootEnvGUID)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		char request[ACTION_MAX+1];
		grub_memset(request, 0, ACTION_MAX+1);

		grub_snprintf(request, REQUESTLENGTH_MAX, "updateclientbootenv#%s", bootEnvGUID);

		// sets the grub_errno
		genericserverrequest(serverip, client_mac, request);
	}

	return grub_errno;
}

grub_err_t setpathprefix(const grub_efi_pxe_ip_address_t* const serverip, grub_efi_mac_t* client_mac, const char* const bootEnvGUID, const char* const pathPrefix)
{
	grub_errno = GRUB_ERR_NONE;

	if(!client_mac || !serverip || !bootEnvGUID || !pathPrefix)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		char request[ACTION_MAX+1];
		grub_memset(request, 0, ACTION_MAX+1);

		grub_snprintf(request, REQUESTLENGTH_MAX, "setpathprefix#%s#%s", bootEnvGUID, pathPrefix);

		// sets the grub_errno
		genericserverrequest(serverip, client_mac, request);
	}

	return grub_errno;
}

grub_err_t generatefilename(char* filename, grub_efi_mac_t* client_mac, const char* uuid, const char* action)
{
	grub_errno = GRUB_ERR_NONE;

	if(!client_mac)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		// 2* because one byte will be converted into 2 ascii characters
		char* mac_address = grub_malloc(2*sizeof(grub_efi_mac_t));

		if(!mac_address)
		{
			grub_error(GRUB_ERR_OUT_OF_MEMORY, "cannot allocate memory for request path!");
		}
		else
		{
			grub_memset(mac_address, 0, 2*sizeof(grub_efi_mac_t));
			if(GRUB_ERR_NONE == convertEfiMacToString(client_mac, mac_address))
			{
				if (uuid)
				{
					grub_snprintf(filename, FILENAME_MAX, "/bbl/%s/%s/%s", mac_address, uuid, action);
				}
				else
				{
					grub_snprintf(filename, FILENAME_MAX, "/bbl/%s/%s/%s", mac_address, "00000000-0000-0000-0000-000000000000", action);
				}


				grub_free(mac_address);

				if (isdebug())
				{
					grub_printf("\nFile request: %s\n", filename);
				}
			}
		}
	}

	return grub_errno;
}

grub_err_t genericserverrequest(const grub_efi_pxe_ip_address_t* server_ip, grub_efi_mac_t* client_mac, const char* const action)
{
	grub_errno = GRUB_ERR_NONE;

	if(!client_mac || !server_ip || !action)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		char filename[FILENAME_MAX + 1];
		grub_memset(filename, 0, FILENAME_MAX+1);

		const char *uuid = NULL;
		getClientUuid(&uuid);

		if(GRUB_ERR_NONE == generatefilename(filename, client_mac, uuid, action))
		{
			grub_uint64_t filesize = 0;
			grub_efi_status_t status = tftpGetFileSize(server_ip, filename, &filesize);
			if(GRUB_EFI_SUCCESS != status)
			{
				grub_error(GRUB_ERR_FILE_READ_ERROR, "Cannot get size of requested file [%s]: error code = %"PRIuGRUB_EFI_UINTN_T, filename, status);
			}
			else
			{
				char* buffer = grub_malloc (filesize);
				if(!buffer)
				{
					grub_error(GRUB_ERR_OUT_OF_MEMORY, "Cannot allocate memory for requested file [%s]", filename);
				}
				else
				{
					status = tftpReadFile(server_ip, filename, buffer, filesize);
					if(GRUB_EFI_SUCCESS != status)
					{
						grub_error(GRUB_ERR_FILE_READ_ERROR, "Cannot read requested file [%s]: error code = %"PRIuGRUB_EFI_UINTN_T, filename, status);
					}
					else
					{
						if (0 != grub_strcasecmp(buffer, "ok"))
						{
							grub_error(GRUB_ERR_NET_INVALID_RESPONSE, "Requested file [%s] responses an error: %s!", filename, buffer);
						}
					}

					grub_free(buffer);
				}
			}
		}
	}

    return grub_errno;
}

grub_err_t loaddynamicbootmenu(grub_efi_pxe_ip_address_t* server_ip, grub_efi_mac_t* client_mac, const grub_uint16_t arch_type, char** buffer)
{
	grub_errno = GRUB_ERR_NONE;

	if(!client_mac || !server_ip || !buffer)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		char action[ACTION_MAX + 1];
		grub_memset(action, 0, ACTION_MAX + 1);

		grub_snprintf(action, ACTION_MAX, "menu#%u", arch_type);

		// sets the grub_errno
		genericbmsfilerequest(server_ip, client_mac, action, buffer);
	}

	return grub_errno;
}

grub_err_t genericbmsfilerequest(const grub_efi_pxe_ip_address_t* server_ip, grub_efi_mac_t* client_mac, const char* const action, char** buffer)
{
	grub_errno = GRUB_ERR_NONE;

	if(!client_mac || !server_ip || !buffer || !action)
	{
		grub_error(GRUB_ERR_BAD_ARGUMENT, "invalid argument (null)!");
	}
	else
	{
		// reset the buffer
		*buffer = NULL;

		char filename[FILENAME_MAX + 1];
		grub_memset(filename, 0, FILENAME_MAX+1);

		const char *uuid = NULL;
		getClientUuid(&uuid);

		if(GRUB_ERR_NONE == generatefilename(filename, client_mac, uuid, action))
		{
			grub_uint64_t filesize = 0;
			grub_efi_status_t status = tftpGetFileSize(server_ip, filename, &filesize);
			if(GRUB_EFI_SUCCESS != status)
			{
				grub_error(GRUB_ERR_FILE_READ_ERROR, "Cannot get size of requested file [%s]: error code = %"PRIuGRUB_EFI_UINTN_T, filename, status);
			}
			else
			{
				char* buffer_tmp = grub_zalloc (filesize+1);
				if(!buffer_tmp)
				{
					grub_error(GRUB_ERR_OUT_OF_MEMORY, "Cannot allocate memory for requested file [%s]", filename);
				}
				else
				{
					status = tftpReadFile(server_ip, filename, buffer_tmp, filesize);
					if(GRUB_EFI_SUCCESS != status)
					{
						grub_error(GRUB_ERR_FILE_READ_ERROR, "Cannot read requested file [%s]: error code = %"PRIuGRUB_EFI_UINTN_T, filename, status);
					}
					else
					{
						*buffer = buffer_tmp;
						buffer_tmp = NULL;
					}
				}

				// buffer_tmp will be NULL if the action succeeds otherwise we have to free it
				if(buffer_tmp)
					grub_free(buffer_tmp);
			}
		}
	}

	return grub_errno;
}
