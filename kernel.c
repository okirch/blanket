/*
 * Reading the procfs
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "blanket.h"

struct sc_procfs_fd {
	FILE *		fp;
	char		buffer[256];
};

static unsigned int
split_line(char *buffer, const char *sepa, char **vec, unsigned int max)
{
	unsigned int n;
	char *s;

	for (n = 0, s = strtok(buffer, sepa); s && n < max - 1; s = strtok(NULL, sepa))
		vec[n++] = s;
	vec[n] = NULL;
	return n;
}

static sc_object_entry_t *
sc_mapping_parse_exec(char *buffer)
{
	static sc_object_entry_t entry;
	char *vec[64];
	char *s;

	memset(&entry, 0, sizeof(entry));

	if (split_line(buffer, " \t\n", vec, 64) < 6)
		return NULL;

	/* permissions: rw-p or r-xp
	 * we're interested in executable mappings only */
	s = vec[1];
	if (s[2] != 'x')
		return NULL;

	/* address range: 55f40f6f5000-55f40f6fd000 */
	entry.start_addr = (caddr_t) strtoul(vec[0], &s, 16);
	if (*s != '-')
		return NULL;
	entry.end_addr = (caddr_t) strtoul(s + 1, &s, 16);
	if (*s != '\0')
		return NULL;

	/* device: 00:28 */
	entry.file.dev = strtoul(vec[3], &s, 16) << 8;
	if (*s != ':')
		return NULL;
	entry.file.dev |= strtoul(s + 1, &s, 16);
	if (*s != '\0')
		return NULL;

	/* inode: decimal long */
	entry.file.ino = strtoul(vec[4], &s, 0);
	if (*s != '\0')
		return NULL;

	/* beware, we do not duplicate the string. */
	entry.file.path = vec[5];
	return &entry;
}

static sc_procfs_fd_t *
sc_procfs_fopen(const char *path)
{
	sc_procfs_fd_t *handle;
	FILE *fp;

	if ((fp = fopen(path, "r")) == NULL) {
		fprintf(stderr, "Could not open %s: %m\n", path);
		return NULL;
	}

	handle = calloc(1, sizeof(*handle));
	handle->fp = fp;

	return handle;
}

void
sc_procfs_fclose(sc_procfs_fd_t *handle)
{
	if (handle->fp != NULL)
		fclose(handle->fp);
	handle->fp = NULL;
	free(handle->fp);
}

sc_procfs_fd_t *
sc_procfs_maps_open(void)
{
	return sc_procfs_fopen("/proc/self/maps");
}

const sc_object_entry_t *
sc_procfs_maps_getent(sc_procfs_fd_t *handle)
{
	while (fgets(handle->buffer, sizeof(handle->buffer), handle->fp) != NULL) {
		const sc_object_entry_t *entry;

		if ((entry = sc_mapping_parse_exec(handle->buffer)) != NULL)
			return entry;
	}

	return NULL;
}
