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
	dst->path = (path && path[0])? strdup(path) : NULL;
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

typedef struct {
	const char *	name;
	int		value;
} sc_constant_t;

static bool
sc_constant_parse(const sc_constant_t *v, const char *string, int *ret_p)
{
	for (; v->name; ++v) {
		if (!strcmp(v->name, string)) {
			*ret_p = v->value;
			return true;
		}
	}
	return false;
}

static const char *
sc_constant_string(const sc_constant_t *v, int value)
{
	static char buffer[16];

	for (; v->name; ++v) {
		if (v->value == value)
			return v->name;
	}
	snprintf(buffer, sizeof(buffer), "%u", value);
	return buffer;
}

static sc_constant_t	sc_mode_constants[] = {
	{ "touch",	SC_MODE_TOUCH },
	{ "timer",	SC_MODE_TIMER },
	{ "mcount",	SC_MODE_MCOUNT },
	{ "ptrace",	SC_MODE_PTRACE },
	{ "plt",	SC_MODE_PLT },

	{ NULL, }
};

bool
sc_string_to_mode(const char *string, int *ret_p)
{
	return sc_constant_parse(sc_mode_constants, string, ret_p);
}

const char *
sc_mode_to_string(int mode)
{
	return sc_constant_string(sc_mode_constants, mode);
}
