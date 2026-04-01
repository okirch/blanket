/*
 * Read/write the control file
 */

#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blanket.h"

const char *	sc_control_path;

sc_control_t *
sc_control_read(const char *path)
{
	sc_control_t *ctl;
	FILE *fp;

	if (path == NULL)
		path = sc_control_path;
	if (path == NULL)
		path = SC_CONTROL_PATH_DEFAULT;

	if (!(fp = fopen(path, "r")))
		return NULL;

	ctl = calloc(1, sizeof(*ctl));
	(void) fread(ctl, sizeof(*ctl), 1, fp);
	fclose(fp);

	{
		/* /bin/bash on my system */
		sc_control_add_dev_ino(ctl, 0x28, 254134);
		/* libc on my system */
		sc_control_add_dev_ino(ctl, 0x28, 409014);
		/* libm on my system */
		sc_control_add_dev_ino(ctl, 0x28, 409017);
	}

	if (ctl->granularity & (ctl->granularity - 1))
		return NULL; /* granularity not a power of 2 */

	ctl->addr_shift = ffsl(ctl->granularity);

	return ctl;
}

sc_control_t *
sc_control_create(void)
{
	sc_control_t *ctx;

	ctx = calloc(1, sizeof(*ctx));
	ctx->format = SC_CONTROL_FILE_VERSION;
	ctx->granularity = SC_DEFAULT_GRANULARITY;
	return ctx;
}

int
sc_control_write(const char *path, sc_control_t *ctl)
{
	FILE *fp;

	if (path == NULL)
		path = sc_control_path;
	if (path == NULL)
		path = SC_CONTROL_PATH_DEFAULT;

	if (!(fp = fopen(path, "w"))) {
		fprintf(stderr, "Cannot write control file %s: %m\n", path);
		return -1;
	}

	fwrite(ctl, sizeof(*ctl), 1, fp);
	fclose(fp);
	return 0;
}

int
sc_control_add_file(sc_control_t *ctx, const char *path)
{
	return -1;
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
