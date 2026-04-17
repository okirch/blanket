/*
 * In-memory tracking of ELF objects.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include "blanket.h"

/*
 * Clone an object entry
 */
sc_object_entry_t *
sc_object_entry_clone(const sc_object_entry_t *entry)
{
	sc_object_entry_t *e;

	e = calloc(1, sizeof(*e));
	e->dev = entry->dev;
	e->ino = entry->ino;
	e->start_addr = entry->start_addr;
	e->end_addr = entry->end_addr;
	e->mode = entry->mode;
	e->addr_shift = entry->addr_shift;
	e->test_id = entry->test_id;

	if (entry->path != NULL)
		e->path = strdup(entry->path);
	return e;
}

static void
sc_object_entry_populate_header(const sc_object_entry_t *entry)
{
	sc_output_header_t *hdr = entry->map_base;

	if (!hdr)
		return;

	hdr->format = SC_CONTROL_FILE_VERSION;
	hdr->dev = entry->dev;
	hdr->ino = entry->ino;
	hdr->addr_shift = entry->addr_shift;
	hdr->mode = entry->mode;

	if (entry->test_id)
		memcpy(hdr->test_id, entry->test_id, sizeof(hdr->test_id) - 1);

	memcpy(hdr->magic, entry->magic, sizeof(hdr->magic));

	gettimeofday(&hdr->timestamp, NULL);
	strncpy(hdr->path, entry->path, sizeof(hdr->path) - 1);
}

static bool
sc_object_entry_from_header(sc_object_entry_t *entry)
{
	sc_output_header_t *hdr = entry->map_base;

	if (hdr->format != SC_CONTROL_FILE_VERSION)
		return false;

	entry->path = strdup(hdr->path);
	entry->dev = hdr->dev;
	entry->ino = hdr->ino;
	entry->mode = hdr->mode;
	entry->addr_shift = hdr->addr_shift;
	if (hdr->test_id[0])
		entry->test_id = hdr->test_id;

	memcpy(entry->magic, hdr->magic, sizeof(entry->magic));

	return true;
}

void
sc_object_entry_free(sc_object_entry_t *entry)
{
	if (entry->path != NULL)
		free(entry->path);
	if (entry->map_base)
		munmap(entry->map_base, entry->map_len);
	free(entry);
}

static int
sc_object_entry_open_write(const sc_object_entry_t *entry)
{
	static int pid_wraparound = 0;
	static pid_t last_pid = 0;
	char namebuf[256];
	time_t now = time(NULL);
	pid_t pid = getpid();

	if (pid < last_pid)
		pid_wraparound += 1;
	last_pid = pid;

	snprintf(namebuf, sizeof(namebuf), "/tmp/coverage-%04x:%08lx-%d:%ld-%lu.map",
			entry->dev, (long) entry->ino,
			pid_wraparound, (long) pid,
			(long) now);

	return open(namebuf, O_CREAT|O_RDWR|O_EXCL, 0600);
}

void *
sc_object_entry_map_write(sc_object_entry_t *entry)
{
	unsigned int granularity = 1 << entry->addr_shift;
	unsigned long num_counters, map_len;
	int fd;

	if (entry->end_addr <= entry->start_addr)
		return NULL; /* shouldn't happen either */

	num_counters = (entry->end_addr - entry->start_addr + granularity - 1) >> entry->addr_shift;
	map_len = SC_OUTPUT_HEADER_SIZE + sizeof(entry->counters[0]) * num_counters;

	if ((fd = sc_object_entry_open_write(entry)) < 0)
		return NULL;

	if (ftruncate(fd, map_len) < 0) {
		close(fd);
		return NULL;
	}

	entry->map_base = mmap(NULL, map_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);

	if (entry->map_base == NULL)
		return NULL;

	entry->counters = (uint32_t *) (entry->map_base + SC_OUTPUT_HEADER_SIZE);
	entry->map_len = map_len;
	entry->num_counters = num_counters;

	sc_object_entry_populate_header(entry);

	return entry->map_base;
}

sc_object_entry_t *
sc_object_entry_load(const char *path)
{
	sc_object_entry_t* entry;
	struct stat stb;
	unsigned int map_len;
	int fd;

	if ((fd = open(path, O_RDONLY)) < 0) {
		fprintf(stderr, "Cannot open %s: %m\n", path);
		return NULL;
	}

	if (fstat(fd, &stb) < 0) {
		perror("fstat");
		close(fd);
		return NULL;
	}

	if (stb.st_size < SC_OUTPUT_HEADER_SIZE) {
		fprintf(stderr, "%s: file too short\n", path);
		close(fd);
		return NULL;
	}

	entry = calloc(1, sizeof(*entry));
	map_len = stb.st_size;

	entry->map_base = mmap(NULL, map_len, PROT_READ, MAP_SHARED, fd, 0);
	entry->map_len = map_len;

	close(fd);

	if (!sc_object_entry_from_header(entry)) {
		fprintf(stderr, "%s: incompatible file format\n", path);
		sc_object_entry_free(entry);
		return NULL;
	}

	entry->counters = (uint32_t *) (entry->map_base + SC_OUTPUT_HEADER_SIZE);
	entry->num_counters = (map_len - SC_OUTPUT_HEADER_SIZE) / sizeof(entry->counters[0]);
	return entry;
}

void
sc_object_entry_flush(sc_object_entry_t *entry)
{
}
