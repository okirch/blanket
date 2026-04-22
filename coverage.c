/*
 * Cooked coverage information for reporting, including per-symbol information.
 *
 * Copyright (C) 2025 SUSE Linux
 * Written by okir@suse.com
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <assert.h>

#include <elf.h>
#include <gelf.h>

#include "blanket.h"

/*
 * Generic symbol table
 */
void
sc_coverage_add_symbol(sc_coverage_t *coverage, const char *name, unsigned long offset, unsigned long size)
{
	static const unsigned int chunk_size = 64;
	sc_symbol_t *sym;

	if ((coverage->nsymbols % chunk_size) == 0)
		coverage->symbol = realloc(coverage->symbol, (coverage->nsymbols + chunk_size) * sizeof(coverage->symbol[0]));

	sym = &coverage->symbol[coverage->nsymbols++];
	memset(sym, 0, sizeof(*sym));
	sym->name = name;
	sym->start_offset = offset;
	sym->end_offset = offset + size;
	// printf("%08lx - %08lx %s\n", sym->start_offset, sym->end_offset, name);
}

static inline bool
sc_symbol_covers(const sc_symbol_t *sym, unsigned long offset)
{
	return (sym->start_offset <= offset && offset < sym->end_offset);
}

static sc_symbol_t *
sc_coverage_find_symbol(sc_coverage_t *coverage, unsigned long offset)
{
	unsigned int i0, i1, i;
	sc_symbol_t *sym;

	i0 = 0;
	i1 = coverage->nsymbols;

	while (i0 + 1 < i1) {
		i = (i0 + i1) / 2;

		sym = &coverage->symbol[i];
		if (offset < sym->start_offset)
			i1 = i;
		else if (sym->end_offset <= offset)
			i0 = i;
		else
			return sym;
	}

	while (i0 < i1) {
		sym = &coverage->symbol[i0++];

		if (sc_symbol_covers(sym, offset))
			return sym;
	}

	return &coverage->unknown_symbol;
}

void
sc_coverage_free(sc_coverage_t *coverage)
{
	if (coverage->symbol)
		free(coverage->symbol);
	free(coverage);
}

/*
 * Sort the symbol table for bsearch lookup
 */
static int
sc_symbol_cmp(const void *a, const void *b)
{
	const sc_symbol_t *sym_a = (const sc_symbol_t *) a;
	const sc_symbol_t *sym_b = (const sc_symbol_t *) b;
	long diff;

	diff = sym_a->start_offset - sym_b->start_offset;
	if (diff < 0)
		return -1;
	if (diff > 0)
		return 1;
	return 0;
}

void
sc_symbol_table_sort(sc_coverage_t *coverage)
{
	qsort(coverage->symbol, coverage->nsymbols, sizeof(coverage->symbol[0]), sc_symbol_cmp);
}

sc_source_file_t *
sc_coverage_add_source_file(sc_coverage_t *coverage, unsigned int compile_unit, const char *filename)
{
	sc_source_file_t *sf;
	unsigned int i;

	for (i = 0; i < coverage->nsourcefiles; ++i) {
		sf = &coverage->sourcefiles[i];

		if (sf->compile_unit == compile_unit && !strcmp(sf->filename, filename))
			return sf;
	}

        if ((coverage->nsourcefiles % 8) == 0)
		coverage->sourcefiles = realloc(coverage->sourcefiles, (coverage->nsourcefiles + 8) * sizeof(coverage->sourcefiles[0]));

	sf = &coverage->sourcefiles[coverage->nsourcefiles++];
        memset(sf, 0, sizeof(*sf));

	sf->compile_unit = compile_unit;
	sf->filename = strdup(filename);

	return sf;
}

void
sc_source_file_add_line_hit(sc_source_file_t *sf, unsigned int lineno)
{
	unsigned int index = lineno / 32;

	if (index >= sf->nwords) {
		unsigned int nwords = (index + 16) & ~15;

		sf->linemap = realloc(sf->linemap, nwords * sizeof(sf->linemap[0]));
		while (sf->nwords < nwords)
			sf->linemap[sf->nwords++] = 0;
	}

	sf->linemap[index] |= (1 << (lineno % 32));
}

sc_coverage_t *
sc_coverage_extract(const sc_object_entry_t *entry, int flags)
{
	sc_coverage_t *coverage;
	unsigned int first_index, last_index, i;
	unsigned int num_hits;
	sc_symbol_t *sym = NULL;

	if (memcmp(entry->magic, "\177ELF", 4)) {
		fprintf(stderr, "%s: does not look like an ELF binary\n", sc_object_entry_get_path(entry));
		return NULL;
	}

	coverage = calloc(1, sizeof(*coverage));

	sc_elf_extract_symbols(entry, coverage);

	sc_symbol_table_sort(coverage);

	if (coverage->text_size == 0) {
		fprintf(stderr, "%s: no .text section found\n", sc_object_entry_get_path(entry));
		sc_coverage_free(coverage);
		return NULL;
	}

	first_index = coverage->text_offset >> entry->addr_shift;
	last_index = ((coverage->text_offset + coverage->text_size + (1 << entry->addr_shift) + 1) >> entry->addr_shift);

	for (i = first_index, num_hits = 0; i <= last_index; ++i) {
		long addr = i << entry->addr_shift;

		if (i >= entry->num_counters)
			break;

		if (entry->counters[i] == 0)
			continue;

		num_hits++;

		if (sym == NULL || !sc_symbol_covers(sym, addr))
			sym = sc_coverage_find_symbol(coverage, addr);

		if (sym != NULL)
			sym->num_hits += 1;
	}

	for (i = 0; i < coverage->nsymbols; ++i) {
		sc_symbol_t *sym = &coverage->symbol[i];
		unsigned int size;

		size = (sym->end_offset >> entry->addr_shift) - (sym->start_offset >> entry->addr_shift);
		sym->coverage = 100.0 * sym->num_hits / size;
	}

	coverage->global_hits = num_hits;
	coverage->global_coverage = 100.0 * num_hits / (last_index - first_index + 1);

	if (flags & SC_COVERAGE_SOURCE)
		sc_dwarf_extract_coverage(entry, coverage);

	return coverage;
}

