/*
 * Manage the tracking context during analysis.
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

static __thread sc_object_entry_t *sc_last_object;

sc_context_t *
sc_context_init(const sc_control_t *ctl)
{
	/* Do not allow duplicate init */
	if (sc_context != NULL)
		return NULL;

	sc_context = calloc(1, sizeof(*sc_context));
	sc_context->control = ctl;

	return sc_context;
}

static void
sc_context_enable_object(const sc_context_t *ctx, sc_object_entry_t *entry)
{
	if (!(entry->flags & SC_CONTEXT_ACTIVE_F)) {
		memcpy(entry->magic, entry->start_addr, 8);
		entry->addr_shift = sc_context_get_addr_shift(ctx);
		entry->test_id = sc_context_get_test_id(ctx);
		if (sc_object_entry_map_write(entry)) {
			if (sc_tracing)
				printf("Enabled %s; granularity %u; addr_shift %u\n", entry->path,
						sc_context_get_granularity(ctx),
						entry->addr_shift);
			entry->flags |= SC_CONTEXT_ACTIVE_F;
		}
	}
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
	entry->mode = sc_context_get_mode(ctx);
	entry->flags |= SC_CONTEXT_SEEN_F;
}

void
sc_context_update_mapping(sc_context_t *ctx, const sc_object_entry_t *entry)
{
	const sc_control_t *ctl = ctx->control;
	unsigned int i;

	/* Check if we're interested in tracing this */
	if (ctl->measure_all) {
		if (sc_tracing)
			printf("Tracing all objects\n");
	} else
	if (sc_control_get_entry(ctl, entry->dev, entry->ino, entry->path)) {
		if (sc_tracing)
			printf("Tracking %s\n", entry->path);
	} else {
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

	while ((map_entry = sc_procfs_maps_getent(mapfp)) != NULL) {
		/* ignore [vdso] */
		if (map_entry->path[0] == '[')
			continue;

		sc_context_update_mapping(ctx, map_entry);
	}

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

	return ctx->num_entries;
}

void
sc_context_add_sample(sc_context_t *ctx, caddr_t addr)
{
	unsigned int i, i0 = 0, i1 = ctx->num_entries;
	sc_object_entry_t *entry;
	unsigned int n;

	if ((entry = sc_last_object) != NULL
	 && entry->start_addr <= addr && addr < entry->end_addr)
		goto use_entry;

	while (i0 + 1 < i1) {
		i = (i0 + i1) / 2;
		entry = ctx->entries[i];
		if (addr < entry->start_addr) {
			i1 = i;
		} else if (entry->end_addr < addr) {
			i0 = i;
		} else {
			goto use_entry;
		}
	}

	while (i0 < i1) {
		entry = ctx->entries[i0++];
		if (entry->start_addr <= addr && addr < entry->end_addr)
			goto use_entry;
	}
	return;

use_entry:
	/* Note, the counter updates are not atomic; so in a multithreaded
	 * application, there will be race conditions. However, as we do not
	 * care about the exact set of hits per address at the end of the day,
	 * this hopefully does not matter.
	 *
	 * If we ever change the sampling code to be just a bit field, where
	 * each bit reprents one memory bucket, then we need to start worrying
	 * about atomic updates.
	 */
	n = (addr - entry->start_addr) >> entry->addr_shift;

	/* the condition should always be true, but better safe than sorry. */
	if (n < entry->num_counters)
		entry->counters[n] += 1;

	sc_last_object = entry;
}

