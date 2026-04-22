/*
 * Utility functions
 */

#include <string.h>
#include <stdio.h>
#include "blanket.h"

bool
sc_squeeze_path(const char *path, char *buffer, size_t bufsz)
{
	const char *prefix = "", *slash;

	while (strlen(prefix) + strlen(path) >= bufsz) {
		if ((slash = strchr(path, '/')) == NULL) {
			strncpy(buffer, "(too long)", bufsz - 1);
			return false;
		}
		while (*slash == '/')
			++slash;
		prefix = ".../";
	}

	snprintf(buffer, bufsz, "%s%s", prefix, path);
	return true;
}

