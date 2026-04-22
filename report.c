/*
 * Reporting functions
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <unistd.h>
#include "blanket.h"

/*
 * source file coverage reporting.
 * We have a simple mode, where we just print line numbers, and an annotated
 * mode where we display the source code itself.
 */
typedef struct sc_source_renderer {
	sc_source_file_t *	file;
	unsigned int		context_lines;
	unsigned int		last_line;
	FILE *			fp;

	bool			terminfo_initialized;
	char *			smso;
	char *			rmso;

	void			(*open)(struct sc_source_renderer *, sc_source_file_t *);
	void			(*line_hit)(struct sc_source_renderer *, unsigned int line);
	void			(*close)(struct sc_source_renderer *);
} sc_source_renderer_t;

static void
simple_source_renderer_open(sc_source_renderer_t *r, sc_source_file_t *f)
{
	printf("%s\n", f->filename);
}

static void
simple_source_renderer_line_hit(sc_source_renderer_t *r, unsigned int line)
{
	printf("  %u\n", line);
}

static void
simple_source_renderer_close(sc_source_renderer_t *r)
{
	/* NOP */
}

static sc_source_renderer_t	simple_source_renderer = {
	.open = simple_source_renderer_open,
	.line_hit = simple_source_renderer_line_hit,
	.close = simple_source_renderer_close,
};

static void
annotated_source_renderer_open(sc_source_renderer_t *r, sc_source_file_t *f)
{
	printf("%s\n", f->filename);

	r->fp = fopen(f->filename, "r");
	if (r->fp == NULL)
		printf("  Warning: cannot open source file: %m\n");
	r->last_line = 0;

	if (!r->terminfo_initialized) {
		int err;

		r->terminfo_initialized = true;

		if (isatty(fileno(stdout)) && setupterm(NULL, fileno(stdout), &err) >= 0) {
			r->smso = tigetstr("bold");
			r->rmso = tigetstr("sgr0");
		}
		if (r->smso == NULL || r->rmso == NULL)
			r->smso = r->rmso = NULL;
	}
}

static void
annotated_source_renderer_line_hit(sc_source_renderer_t *r, unsigned int line)
{
	char linebuf[512];
	unsigned int header = 0, trailer = 0, gap = 0;

	if (r->fp == NULL) {
print_simple:
		printf("  %u\n", line);
		return;
	}

	if (line <= r->last_line)
		return;

	if (r->last_line != 0)
		trailer = r->last_line + r->context_lines;
	if (line > r->context_lines)
		header = line - r->context_lines;

	while (r->last_line < line) {
		if (fgets(linebuf, sizeof(linebuf), r->fp) == 0) {
			printf("(premature end of file)\n");
			fclose(r->fp);
			r->fp = NULL;
			goto print_simple;
		}

		linebuf[strcspn(linebuf, "\n")] = '\0';

		r->last_line += 1;

		if (r->last_line == line) {
			if (r->smso)
				putp(r->smso);
			printf(" + %s\n", linebuf);
			if (r->rmso)
				putp(r->rmso);
		} else
		if (header <= r->last_line) {
			if (gap) {
				printf("--- line %u: ---\n", header);
				gap = 0;
			}
			printf("   %s\n", linebuf);
		} else
		if (r->last_line < trailer) {
			printf("   %s\n", linebuf);
		} else {
			gap = 1;
		}
	}
}

static void
annotated_source_renderer_close(sc_source_renderer_t *r)
{
	if (r->fp) {
		if (r->last_line > 0) {
			char linebuf[512];
			unsigned int k;

			for (k = 0; k < r->context_lines; ++k) {
				if (fgets(linebuf, sizeof(linebuf), r->fp) == 0)
					break;
				linebuf[strcspn(linebuf, "\n")] = '\0';
				printf("   %s\n", linebuf);
			}
		}

		fclose(r->fp);
		r->fp = NULL;
	}
}

static sc_source_renderer_t	annotated_source_renderer = {
	.context_lines = 3,
	.open = annotated_source_renderer_open,
	.line_hit = annotated_source_renderer_line_hit,
	.close = annotated_source_renderer_close,
};

typedef struct {
	sc_object_reference_t	file;

	unsigned int		num_hits;
} sc_object_touch_t;

struct sc_report {
	int			details;

	unsigned int		num_objects;
	sc_object_touch_t *	objects;
};

static void
sc_report_add_touched_object(sc_report_t *report, const sc_object_entry_t *entry)
{
	sc_object_touch_t *ob;
	unsigned int i;

	for (i = 0; i < report->num_objects; ++i) {
		ob = &report->objects[i];
		if (sc_object_reference_same(&ob->file, &entry->file)) {
			ob->num_hits++;
			return;
		}
	}

	if ((report->num_objects % 16) == 0)
		report->objects = realloc(report->objects, (report->num_objects + 16) * sizeof(report->objects[0]));

	ob = &report->objects[report->num_objects++];
	memset(ob, 0, sizeof(*ob));

	sc_object_reference_copy(&ob->file, &entry->file);
	ob->num_hits = 1;
}

sc_report_t *
sc_report_alloc(int details)
{
	sc_report_t *report;

	report = calloc(1, sizeof(*report));
	report->details = details;
	return report;
}

void
sc_report_trailer(sc_report_t *report)
{
	unsigned int i;

	for (i = 0; i < report->num_objects; ++i) {
		sc_object_touch_t *ob = &report->objects[i];

		printf(" %3u %s\n", ob->num_hits, ob->file.path);
	}
}

int
sc_report_process_file(sc_report_t *report, const char *path)
{
	sc_object_entry_t *entry;
	sc_coverage_t *coverage;
	int flags = 0;

	entry = sc_object_entry_load(path);
	if (entry == NULL) {
		fprintf(stderr, "Could not load %s\n", path);
		return -1;
	}

	if (entry->mode == SC_MODE_TOUCH) {
		sc_report_add_touched_object(report, entry);
		return 0;
	}

	if (report->details & SC_DETAIL_SOURCELINES)
		flags |= SC_COVERAGE_SOURCE;

	coverage = sc_coverage_extract(entry, flags);
	if (coverage == NULL)
		return -1;

	printf("%s\n", entry->file.path);
	printf("Mode:             %u\n", entry->mode);
	printf("ELF text section: %08lx-%08lx\n",
			coverage->text_offset,
			coverage->text_offset + coverage->text_size);
	if (entry->test_id != NULL)
		printf("Test ID:          %s\n", entry->test_id);
	printf("Sampling size:    %u\n", 1 << entry->addr_shift);

	if (entry->mode == SC_MODE_TIMER)
		printf("Global coverage:  %.2f%%\n", coverage->global_coverage);
	else
		printf("Global coverage:  %u hits\n", coverage->global_hits);

	if (report->details & SC_DETAIL_SYMBOLS) {
		unsigned int i;

		printf("Symbols and their coverage:\n");
		for (i = 0; i < coverage->nsymbols; ++i) {
			sc_symbol_t *sym = &coverage->symbol[i];

			if (sym->num_hits) {
				if (entry->mode == SC_MODE_TIMER)
					printf("  %5.1f%% %s\n", sym->coverage, sym->name);
				else
					printf("  %5u %s\n", sym->num_hits, sym->name);
			}
		}

		if (coverage->unknown_symbol.num_hits)
			printf("  %6u hits in code without symbol\n", coverage->unknown_symbol.num_hits);
	}

	if (report->details & SC_DETAIL_SOURCELINES) {
		sc_source_renderer_t *renderer = &simple_source_renderer;
		unsigned int i, j;

		if (report->details & SC_DETAIL_ANNOTATE)
			renderer = &annotated_source_renderer;

		printf("Source files and their coverage:\n");
		simple_source_renderer.file = NULL;
		for (i = 0; i < coverage->nsourcefiles; ++i) {
			sc_source_file_t *file = &coverage->sourcefiles[i];

			renderer->open(renderer, file);
			for (j = 0; j < file->nwords; ++j) {
				uint32_t word = file->linemap[j], mask;
				unsigned int line = 32 * j;

				for (mask = 1; mask; mask <<= 1, ++line) {
					if (word & mask)
						renderer->line_hit(renderer, line);
				}
			}
			renderer->close(renderer);
		}
	}

	printf("\n");

	sc_coverage_free(coverage);
	return 0;
}



