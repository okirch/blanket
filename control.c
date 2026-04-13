/*
 * Read/write the control file
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blanket.h"

const char *	sc_control_path = SC_CONTROL_PATH_DEFAULT;

void
sc_control_set_path(const char *path)
{
	if (path == NULL)
		path = secure_getenv("BLANKET_CONTROL");
	if (path == NULL)
		path = SC_CONTROL_PATH_DEFAULT;

	sc_control_path = path;
}

static sc_control_t *
__sc_control_read(int quiet)
{
	sc_control_t *ctl;
	FILE *fp;

	if (sc_control_path == NULL) {
		if (!quiet)
			fprintf(stderr, "Cannot read control file: no path\n");
		return NULL;
	}

	if (!(fp = fopen(sc_control_path, "r"))) {
		if (!quiet)
			fprintf(stderr, "Could not read control file %s: %m\n",
					sc_control_path);
		return NULL;
	}

	ctl = calloc(1, sizeof(*ctl));
	(void) fread(ctl, sizeof(*ctl), 1, fp);
	fclose(fp);

	if (ctl->granularity != (1 << ctl->addr_shift)) {
		if (!quiet)
			fprintf(stderr, "%s: address shift %u does not match granularity %u\n",
					sc_control_path, ctl->addr_shift, ctl->granularity);
		return NULL; /* mismatch */
	}

	if (ctl->format != SC_CONTROL_FILE_VERSION) {
		if (!quiet)
			fprintf(stderr, "%s: file format mismatch; I support %04x, found %04x\n",
					sc_control_path, SC_CONTROL_FILE_VERSION, ctl->format);
		return NULL; /* mismatch */
	}

	return ctl;
}

sc_control_t *
sc_control_read(void)
{
	return __sc_control_read(0);
}

sc_control_t *
sc_control_read_quiet(void)
{
	return __sc_control_read(1);
}

sc_control_t *
sc_control_create(void)
{
	sc_control_t *ctl;

	ctl = calloc(1, sizeof(*ctl));
	ctl->format = SC_CONTROL_FILE_VERSION;
	ctl->granularity = SC_DEFAULT_GRANULARITY;
	ctl->addr_shift = ffsl(ctl->granularity) - 1;
	return ctl;
}

int
sc_control_write(sc_control_t *ctl)
{
	FILE *fp;

	if (sc_control_path == NULL) {
		fprintf(stderr, "Cannot write control file: no path\n");
		return -1;
	}

	if (!(fp = fopen(sc_control_path, "w"))) {
		fprintf(stderr, "Cannot write control file %s: %m\n", sc_control_path);
		return -1;
	}

	fwrite(ctl, sizeof(*ctl), 1, fp);
	fclose(fp);
	return 0;
}

int
sc_control_add_file(sc_control_t *ctl, const char *path)
{
	struct stat stb;
	sc_control_entry_t *entry;

	if (stat(path, &stb) < 0) {
		fprintf(stderr, "could not add %s: %m\n", path);
		return -1;
	}

	if (ctl->num_entries >= SC_CONTROL_MAX_ENTRIES) {
		fprintf(stderr, "could not add %s: too many entries in control file\n", path);
		return -1;
	}

	entry = &ctl->entry[ctl->num_entries++];
	entry->dev = stb.st_dev;
	entry->ino = stb.st_ino;
	return 0;
}

int
sc_control_add_dev_ino(sc_control_t *ctx, dev_t dev, ino_t ino)
{
	unsigned int i = ctx->num_entries;

	if (i >= SC_CONTROL_MAX_ENTRIES)
		return -1;

	ctx->entry[i].dev = dev;
	ctx->entry[i].ino = ino;
	ctx->num_entries++;
	return 0;
}

const sc_control_entry_t *
sc_control_get_entry(const sc_control_t *ctl, dev_t dev, ino_t ino, const char *path)
{
	unsigned int i;

	for (i = 0; i < ctl->num_entries; ++i) {
		const sc_control_entry_t *e = &ctl->entry[i];

		if (e->dev == dev && e->ino == ino)
			return e;
	}
	return NULL;
}
