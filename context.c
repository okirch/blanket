/*
 * In-memory tracking of ELF objects.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "blanket.h"

#define SC_CONTEXT_ACTIVE_F	0x0001	/* enabled for stats collection */
#define SC_CONTEXT_SEEN_F	0x8000	/* used during rescan */

sc_context_t *			sc_context = NULL;

sc_context_t *
sc_context_init(const sc_control_t *ctl)
{
	/* Do not allow duplicate init */
	if (sc_context != NULL)
		return NULL;

	sc_context = calloc(1, sizeof(*sc_context));
	sc_context->control = ctl;
	sc_context->granularity = ctl->granularity;
	sc_context->test_id = ctl->test_id;

	return sc_context;
}

/*
 * Clone an object entry
 */
static sc_object_entry_t *
sc_object_entry_clone(const sc_object_entry_t *entry)
{
	sc_object_entry_t *e;

	e = calloc(1, sizeof(*e));
	e->dev = entry->dev;
	e->ino = entry->ino;
	e->start_addr = entry->start_addr;
	e->end_addr = entry->end_addr;

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

	gettimeofday(&hdr->timestamp, NULL);
	strncpy(hdr->path, entry->path, sizeof(hdr->path) - 1);
}

static void
sc_object_entry_free(sc_object_entry_t *entry)
{
	if (sc_tracing)
		printf("sc_object_entry_free(%s)\n", entry->path);

	if (entry->path != NULL)
		free(entry->path);
	if (entry->map_base)
		munmap(entry->map_base, entry->map_len);
	free(entry);
}

static int
sc_context_map_open(const sc_context_t *ctx, const sc_object_entry_t *entry)
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

	return open(namebuf, O_CREAT|O_RDWR, 0600);
}

static void *
sc_context_map_output(const sc_context_t *ctx, sc_object_entry_t *entry)
{
	unsigned int granularity = ctx->granularity;
	unsigned long num_counters, map_len;
	int fd;

	if (granularity == 0 || (granularity & (granularity - 1)))
		return NULL; /* shouldn't happen */
	entry->addr_shift = ffsl(granularity);

	if (entry->end_addr <= entry->start_addr)
		return NULL; /* shouldn't happen either */

	num_counters = (entry->end_addr - entry->start_addr + granularity - 1) >> entry->addr_shift;
	map_len = SC_OUTPUT_HEADER_SIZE + sizeof(entry->counters[0]) * num_counters;

	if ((fd = sc_context_map_open(ctx, entry)) < 0)
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

static void
sc_context_enable_object(const sc_context_t *ctx, sc_object_entry_t *entry)
{
	if (!(entry->flags & SC_CONTEXT_ACTIVE_F)) {
		if (sc_context_map_output(ctx, entry)) {
			if (sc_tracing)
				printf("Enabled %s; granularity %u\n", entry->path, ctx->granularity);
			entry->flags |= SC_CONTEXT_ACTIVE_F;
		}
	}
}

static void
sc_object_entry_flush(sc_object_entry_t *entry)
{
}

/*
 * Insert new entry into context
 */
static void
sc_context_add_entry(sc_context_t *ctx, sc_object_entry_t *entry)
{
	static const unsigned int chunk = 8;
	unsigned int where;

	if ((ctx->num_entries % chunk) == 0) {
		ctx->entries = realloc(ctx->entries, (ctx->num_entries + chunk) * sizeof(ctx->entries[0]));
		memset(ctx->entries + ctx->num_entries, 0, chunk * sizeof(ctx->entries[0]));
	}

	for (where = 0; where < ctx->num_entries; ++where) {
		const sc_object_entry_t *e = ctx->entries[where];

		if (entry->start_addr < e->start_addr)
			break;
	}

	if (where < ctx->num_entries)
		memmove(ctx->entries + where + 1, ctx->entries, (ctx->num_entries - where) * sizeof(ctx->entries[0]));

	ctx->entries[where] = entry;
	ctx->num_entries += 1;

	/* printf("Inserted %s at pos %u\n", entry->path, where); */
	entry->flags |= SC_CONTEXT_SEEN_F;
}

void
sc_context_update_mapping(sc_context_t *ctx, const sc_object_entry_t *entry)
{
	const sc_control_t *ctl = ctx->control;
	unsigned int i;

	/* Check if we're interested in tracing this */
	if (!ctl->measure_all
	 && !sc_control_get_entry(ctl, entry->dev, entry->ino, entry->path)) {
		// printf("Ignore %s\n", entry->path);
		return;
	}

	for (i = 0; i < ctx->num_entries; ++i) {
		sc_object_entry_t *e = ctx->entries[i];

		if (e->start_addr == entry->start_addr && e->end_addr == entry->end_addr
		 && e->dev == entry->dev && e->ino == entry->ino) {
			/* FIXME: what would we do if the mapping changed?
			 * Can there be multiple mappings of the same object? */
			if (sc_tracing)
				printf("Existing mapping of %s\n", entry->path);
			e->flags |= SC_CONTEXT_SEEN_F;
			return;
		}
	}

	sc_context_add_entry(ctx, sc_object_entry_clone(entry));
}

int
sc_context_rescan(void)
{
	sc_context_t *ctx;
	sc_procfs_fd_t *mapfp;
	const sc_object_entry_t *map_entry;
	unsigned int i, j;

	if (!(ctx = sc_context))
		return 0;

	for (i = 0; i < ctx->num_entries; ++i) {
		sc_object_entry_t *e = ctx->entries[i];
		e->flags &= ~SC_CONTEXT_SEEN_F;
	}

	if (!(mapfp = sc_procfs_maps_open()))
		return 0;

	while ((map_entry = sc_procfs_maps_getent(mapfp)) != NULL)
		sc_context_update_mapping(ctx, map_entry);

	sc_procfs_fclose(mapfp);

	for (i = j = 0; i < ctx->num_entries; ++i) {
		sc_object_entry_t *e = ctx->entries[i];

		if (e->flags & SC_CONTEXT_SEEN_F) {
			sc_context_enable_object(ctx, e);
			ctx->entries[j++] = e;
		} else {
			sc_object_entry_flush(e);
			sc_object_entry_free(e);
		}
	}

	ctx->num_entries = j;

	return 1;
}
