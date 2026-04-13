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

sc_coverage_t *
sc_coverage_extract(const sc_object_entry_t *entry)
{
	sc_coverage_t *coverage;
	unsigned int first_index, last_index, i;
	unsigned int num_hits;
	sc_symbol_t *sym = NULL;

	if (memcmp(entry->magic, "\177ELF", 4)) {
		fprintf(stderr, "%s: does not look like an ELF binary\n", entry->path);
		return NULL;
	}

	coverage = calloc(1, sizeof(*coverage));

	sc_elf_extract_symbols(entry, coverage);

	sc_symbol_table_sort(coverage);

	if (coverage->text_size == 0) {
		fprintf(stderr, "%s: no .text section found\n", entry->path);
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

	coverage->global_coverage = 100.0 * num_hits / (last_index - first_index + 1);
	return coverage;
}

