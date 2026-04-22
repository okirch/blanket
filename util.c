/*
 * Utility functions
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

void
sc_object_reference_copy(sc_object_reference_t *dst, const sc_object_reference_t *src)
{
	if (!sc_object_reference_same(dst, src)) {
		/* clear, then overwrite */
		sc_object_reference_destroy(dst);
		sc_object_reference_set(dst, src->dev, src->ino, src->path);
	}
}

void
sc_object_reference_set(sc_object_reference_t *dst, dev_t dev, ino_t ino, const char *path)
{
	dst->path = strdup(path);
	dst->dev = dev;
	dst->ino = ino;
}

void
sc_object_reference_destroy(sc_object_reference_t *ref)
{
	if (ref->path)
		free(ref->path);
	ref->path = NULL;
}
