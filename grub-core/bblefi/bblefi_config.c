//////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2015 by baramundi software AG
//////////////////////////////////////////////////////////////////////////////

#include <grub/dl.h>
#include <grub/time.h>
#include <grub/mm.h>
#include "utils.h"
#include <grub/misc.h>
#include "bblefi_config.h"
#include "bara_efi_defines.h"

#define FALSE 0
#define TRUE 1

GRUB_MOD_LICENSE("GPLv3+");

/*******************************************************************************************************************/

int parseline(const char* const filename, char* const line, char* const name, char* const value);
char* file_read_line(char* line, int maxLineLength, char* buffer, grub_off_t* offset, grub_uint64_t bufferLength);

static int g_debug = 0;

/*******************************************************************************************************************/

int isdebug()
{
	return g_debug;
}

void debugdelay(const int seconds)
{
	if (0 != isdebug())
	{
		grub_printf("%d seconds debug delay...\n", seconds);
		grub_sleep(seconds);
	}
}

char* file_read_line(char* line, int maxLineLength, char* buffer, grub_off_t* offset, grub_uint64_t bufferLength)
{
	char* ret = NULL;
	*line = '\0';

	grub_uint64_t old_offset = *offset;
	grub_ssize_t read_bytes = 0;

	// This means the end of buffer was reached
	if (old_offset >= bufferLength)
	{
		return ret;
	}

	if (old_offset + maxLineLength > bufferLength)
	{
		grub_memcpy(line, (const char*)(buffer + old_offset), bufferLength - old_offset);
		read_bytes = (grub_ssize_t)(bufferLength - old_offset);
	}
	else
	{
		grub_memcpy(line, (const char*)(buffer + old_offset), maxLineLength);
		read_bytes = (grub_ssize_t)maxLineLength;
	}

	if(0 < read_bytes)
	{
		char* p;
		for(p = line; p-line < maxLineLength && p-line < read_bytes && (*p) != '\n'; p++);

		line[p-line + 1] = '\0';

		grub_off_t new_offset = old_offset + (p-line + 1);
		if(new_offset > bufferLength)
		{
			line[p-line + 1 - (new_offset - bufferLength)] = '\0';
			new_offset = bufferLength;
		}

		*offset = new_offset;

		ret = line;
	}

	return ret;
}

grub_err_t read_bblefi_config(const grub_efi_pxe_ip_address_t* tftpIp,
		const char* servers[], const char* properties[], const char* values[],
		int* noOfServers)
{
	static const int maxLineLength = MAX_PROPERTYNAMELENGTH
			+ MAX_PROPERTYVALUELENGTH + 1 + 2 + 1;
	char line[MAX_PROPERTYNAMELENGTH + MAX_PROPERTYVALUELENGTH + 1 + 2 + 1 + 1];
	grub_memset(line, 0, maxLineLength + 1);

	char name[MAX_PROPERTYNAMELENGTH + 1];
	grub_memset(name, 0, MAX_PROPERTYNAMELENGTH + 1);

	char value[MAX_PROPERTYVALUELENGTH];
	grub_memset(value, 0, MAX_PROPERTYNAMELENGTH);

	grub_memset(servers, 0, sizeof(char*) * MAX_SERVERS);
	grub_memset(properties, 0, sizeof(char*) * MAX_PROPERTIES);
	grub_memset(values, 0, sizeof(char*) * MAX_PROPERTIES);

	int serverAmount, noOfProperties = 0;

	const char* const filename = "/bblefi.cfg";
	grub_printf("Reading configuration file \"%s\"...\n", filename);

	grub_uint64_t fileSize = 0;

	if (GRUB_EFI_SUCCESS != tftpGetFileSize(tftpIp, filename, &fileSize))
	{
		if (isdebug())
		{
			grub_printf("TFTP-Request failed. Unable to determine size of file: [%s] \n", filename);
		}

		return GRUB_ERR_NONE;
	}

	char* buffer = grub_malloc(fileSize);

	if (!buffer)
	{
		// Which return value should be returned here? Not clear!
		return -1;
	}

	if (GRUB_EFI_SUCCESS != tftpReadFile(tftpIp, filename, buffer, fileSize))
	{
		grub_error(GRUB_ERR_FILE_READ_ERROR, N_("TFTP-Request failed. Unable to determine size of file: [%s]"), filename);
		return GRUB_ERR_FILE_READ_ERROR;
	}

	int eof = FALSE;
	int noOfLines = 0;
	grub_off_t offset = 0;

	serverAmount = 0;
	noOfProperties = 0;

	do
	{
		eof = (NULL == file_read_line(line, maxLineLength, buffer, &offset, fileSize));

		if (eof)
		{
			if (grub_errno != GRUB_ERR_NONE)
			{
				grub_error(GRUB_ERR_FILE_READ_ERROR, "Error reading configuration file \"%s\"", filename);
				grub_print_error();
			}

			if (isdebug())
			{
				grub_printf("\nEnd of configuration file reached, code %d, %d lines\n", grub_errno, noOfLines);
			}

			debugdelay(10);
		}
		else
		{
			noOfLines++;

			if (isdebug())
			{
				grub_printf("> %s", line);
			}

			if (0 != parseline(filename, line, name, value))
			{
				grub_print_error();
			}
			else
			{
				if ('\0' != *name && '\0' != *value)
				{
					/* server= */
					if ((serverAmount < MAX_SERVERS) && (0 == grub_strcasecmp(name, "server")))
					{
						servers[serverAmount++] = grub_strdup(value);
					}

					/* all properties (including server names) */
					if (noOfProperties < MAX_PROPERTIES)
					{
						properties[noOfProperties] = grub_strdup(name);
						values[noOfProperties] = grub_strdup(value);
						noOfProperties++;
					}

					/* debug property is used here */
					if (0 == grub_strcasecmp("debug", name))
					{
						g_debug = grub_strtoul(value, NULL, 10);
					}
				}
			}
		}
	} while (!eof);

	grub_free(buffer);
	buffer = NULL;

	if (isdebug())
	{
		int i = 0;

		grub_printf("The following properties (%d) were recognized:\n", noOfProperties);
		for (i = 0; i < noOfProperties; i++)
		{
			grub_printf("  %s=%s\n", properties[i], values[i]);
		}

		grub_printf("The following server entries (%d) were recognized:\n", serverAmount);
		for (i = 0; i < serverAmount; i++)
		{
			grub_printf("  server=%s\n", servers[i]);
		}
	}

	*noOfServers = serverAmount;
	return GRUB_ERR_NONE;
}

int parseline(const char* const filename, char* const line, char* const name, char* const value)
{
	static int lineNumber = 0;
	int rc = -1;

	// empty value and name for new properties
	*value = '\0';
	*name = '\0';

	trim(line);

	lineNumber++;

	if ('#' == *line || '\0' == *line)
	{
		/* Line is a comment */
		rc = 0;
	}
	else
	{
		char* p = grub_strchr(line, '=');
		if (NULL == p)
		{
			grub_printf("no ==  \n");
			debugdelay(5);
		}
		else
		{
			grub_size_t length = p - line;
			if (length > MAX_PROPERTYNAMELENGTH)
			{
				grub_error(GRUB_ERR_READ_ERROR,
							"Error parsing \"%s\": property name in line %d exceeds maximal length of %d characters",
							filename,
							lineNumber,
							MAX_PROPERTYNAMELENGTH);
			}
			else
			{
				/* Copy property name */
				grub_strncpy(name, line, length);
				name[length] = '\0';
				trim(name);

				length = grub_strlen (p+1);
				if (length > MAX_PROPERTYVALUELENGTH)
				{
					grub_error(GRUB_ERR_READ_ERROR,
								"Error parsing \"%s\": property value in line %d exceeds maximal length of %d characters",
								filename,
								lineNumber,
								MAX_PROPERTYNAMELENGTH);
				}
				else
				{
					/* Copy property value */
					grub_strcpy(value, p+1);
					trim(value);
					rc = 0;
				}
			}
		}
	}

	return rc;
}

const char* valueofproperty(const char* const properties[], const char* const values[], const char* const name)
{
	const char* pValue = NULL;
	int i = 0;

	for (i = 0; (i < MAX_PROPERTIES) && (NULL == pValue); i++)
	{
		if (NULL != properties[i] && NULL != values[i])
		{
			if (0 == grub_strcasecmp(name, properties[i]))
			{
				pValue = values[i];
			}
		}
	}

	return pValue;
}
