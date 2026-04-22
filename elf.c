/*
 * ELF headers
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

#define ALIGN(len, a)		(((len) + (a - 1)) & ~(a - 1))
#define SC_ELF_MAX_SECTIONS	16

typedef struct sc_elf_open_file {
	char *			path;
	int			fd;
	Elf *			elf;
	size_t			shstrndx;
} sc_elf_open_file_t;

typedef struct sc_elf_section {
	uint32_t		index;
	char *			name;
	loff_t			offset;
	size_t			size;
	GElf_Shdr		hdr;

	/* for SHT_SYMTAB sections */
	unsigned int		num_symbols;
} sc_elf_section_t;

typedef struct sc_elf_symbol {
	char *			name;
	GElf_Sym		sym;
} sc_elf_symbol_t;

typedef struct sc_elf_object {
	struct sc_elf_object *	next;
	bool			in_cache;

	sc_object_reference_t	file;

	unsigned int		nsections;
	sc_elf_section_t *	section;

	char *			debug_link;
	uint64_t		debug_build_id;
	struct sc_elf_object *	debug_object;

	unsigned int		nsymbols;
	sc_elf_symbol_t *	symbol;
} sc_elf_object_t;

static sc_elf_object_t *sc_elf_object_cache = NULL;

static void		sc_elf_end(sc_elf_open_file_t *ef);
static void		sc_elf_object_add_symbol(sc_elf_object_t *mapping, const char *name, const GElf_Sym *sym);
static void		sc_elf_object_free(sc_elf_object_t *object);

static sc_elf_open_file_t *
sc_elf_open(const char *path)
{
	sc_elf_open_file_t *ef;
	int fd;

	if ((fd = open(path, O_RDONLY)) < 0)
		return NULL;

	ef = calloc(1, sizeof(*ef));
	ef->path = strdup(path);
	ef->fd = fd;
	return ef;
}

static void
sc_elf_close(sc_elf_open_file_t *ef)
{
	sc_elf_end(ef);
	if (ef->fd >= 0)
		close(ef->fd);
	if (ef->path != NULL)
		free(ef->path);
	free(ef);
}

static void
sc_elf_end(sc_elf_open_file_t *ef)
{
	if (ef->elf != NULL) {
		elf_end(ef->elf);
		ef->elf = NULL;
	}

	/* rewind fd after messing around with ELF headers etc */
	if (ef->fd >= 0)
		lseek(ef->fd, 0, SEEK_SET);
}

static bool
sc_elf_begin(sc_elf_open_file_t *ef)
{
	static bool elf_checked = false;

	if (!elf_checked) {
		if (elf_version(EV_CURRENT)== EV_NONE)
			return false;
		elf_checked = true;
	}

	if (!(ef->elf = elf_begin(ef->fd, ELF_C_READ, NULL)))
		goto failed;

	if (elf_kind(ef->elf) != ELF_K_ELF)
		goto failed;

	if (elf_getshdrstrndx(ef->elf, &ef->shstrndx) != 0)
		goto failed;

	return true;

failed:
	sc_elf_end(ef);
	return false;
}

static inline sc_elf_section_t *
sc_elf_object_add_section(sc_elf_open_file_t *file, sc_elf_object_t *mapping, Elf_Scn *scn)
{
	sc_elf_section_t *s;
	GElf_Shdr shdr;
	const char *name;

	if (gelf_getshdr(scn, &shdr) != &shdr)
		return NULL;

	if ((name = elf_strptr(file->elf, file->shstrndx, shdr.sh_name)) == NULL )
		return NULL;

	if ((mapping->nsections % 8) == 0)
		mapping->section = realloc(mapping->section, (mapping->nsections + 8) * sizeof(mapping->section[0]));

	s = &mapping->section[mapping->nsections++];
	s->hdr = shdr;
	s->index = elf_ndxscn(scn);
	s->name = strdup(name);
	s->offset = shdr.sh_offset;
	s->size = shdr.sh_size;

	if (shdr.sh_type == SHT_SYMTAB) {
		Elf_Data *data;

		s->num_symbols = shdr.sh_size / shdr.sh_entsize;

		for (data = NULL; (data = elf_getdata(scn, data)) != NULL; ) {
			unsigned int i;

			for (i = 0; true; ++i) {
				GElf_Sym *sym, _sym;
				char *name;

				if (!(sym = gelf_getsym(data, i, &_sym)))
					break;

				if ((name = elf_strptr(file->elf, s->hdr.sh_link, sym->st_name)) != NULL)
					sc_elf_object_add_symbol(mapping, name, sym);
			}
		}
	}

	return s;
}

static sc_elf_section_t *
sc_elf_object_get_section(const sc_elf_object_t *mapping, const char *name)
{
	unsigned int i;

	for (i = 0; i < mapping->nsections; ++i) {
		sc_elf_section_t *s = &mapping->section[i];

		if (s->name && !strcmp(s->name, name))
			return s;
	}
	return NULL;
}

static void
sc_elf_object_add_symbol(sc_elf_object_t *mapping, const char *name, const GElf_Sym *sym)
{
	sc_elf_symbol_t *stored;

	/* sorry, but we're not interested in undefined symbols */
	if (sym->st_shndx == STN_UNDEF)
		return;

	/* well, just function symbols actually */
	if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC)
		return;

	if ((mapping->nsymbols % 32) == 0)
		mapping->symbol = realloc(mapping->symbol, (mapping->nsymbols + 32) * sizeof(mapping->symbol[0]));

	stored = &mapping->symbol[mapping->nsymbols++];
	stored->name = strdup(name);
	stored->sym = *sym;
}

static inline unsigned long
sc_elf_section_align(const sc_elf_section_t *section, unsigned long offset)
{
	unsigned int	align = section->hdr.sh_addralign;

	/* make sure alignment is a power of 2 */
	assert(!(align & (align - 1)));

	return ((offset + align - 1) & ~(align - 1));
}

/*
 * .gnu_debuglink contains a filename and a 32bit build id
 */
static inline char *
sc_elf_extract_debug_link(sc_elf_open_file_t *file, const sc_elf_section_t *section, uint64_t *build_id_p)
{
	unsigned char *data = NULL;
	unsigned int k;
	int n;

	if (section->size > 2048)
		return NULL;

	if (lseek64(file->fd, section->offset, SEEK_SET) < 0) {
		perror("lseek64");
		goto failed;
	}

	if ((data = malloc(section->size)) == NULL)
		goto failed;

	n = read(file->fd, data, section->size);
	if (n != section->size)
		goto failed;

	/* find the end of the name */
	for (k = 0; k < section->size && data[k] != 0; ++k)
		;

	/* consume NUL and align */
	k = sc_elf_section_align(section, k + 1);

	if (section->size == k + 4) {
		*build_id_p = *(uint32_t *) (data + k);
	} else
	if (section->size == k + 8) {
		*build_id_p = *(uint64_t *) (data + k);
	} else {
		goto failed;
	}

	return (char *) data;

failed:
	if (data != NULL)
		free(data);
	return NULL;
}

static bool
sc_elf_object_locate_sections(sc_elf_open_file_t *file, sc_elf_object_t *mapping)
{
	Elf_Scn *scn;
	bool rv = true;

	if (!sc_elf_begin(file))
		return false;

	for (scn = NULL; (scn = elf_nextscn(file->elf, scn)) != NULL; ) {
		sc_elf_section_t *s;

		if (!(s = sc_elf_object_add_section(file, mapping, scn))) {
			rv = false;
			continue;
		}
	}

	sc_elf_end(file);
	return rv;
}

static sc_elf_object_t *
sc_elf_object_load(const sc_object_reference_t *file_ref)
{
	sc_elf_object_t *object;
	sc_elf_open_file_t *file;

	if (!(file = sc_elf_open(file_ref->path))) {
		fprintf(stderr, "Cannot open %s: %m\n", file_ref->path);
		return NULL;
	}

	object = calloc(1, sizeof(*object));

	/* Note: the dev/ino of this reference will be those of the
	 * ELF binary at the time the coverage report was created.
	 * We may be running on a different system when we get here..
	 * We should perform some checks to ensure it's still the same
	 * binary - even if the dev/ino have changed. */
	sc_object_reference_copy(&object->file, file_ref);

	if (!sc_elf_object_locate_sections(file, object)) {
		fprintf(stderr, "Cannot find .text section in %s\n", file_ref->path);
		sc_elf_object_free(object);
		object = NULL;
	} else {
		sc_elf_section_t *section;

		section = sc_elf_object_get_section(object, ".gnu_debuglink");
		if (section) {
			uint64_t build_id = 0;
			char *name;

			name = sc_elf_extract_debug_link(file, section, &build_id);
			if (name != NULL) {
				object->debug_link = name;
				object->debug_build_id = build_id;
			}
		}
	}

	sc_elf_close(file);
	return object;
}

static void
sc_elf_object_free(sc_elf_object_t *object)
{
	assert(!object->in_cache);

	/* TODO: do a better job at freeing up all memory */
	free(object);
}

/*
 * The debug link string will be something like sha256sum-$version-$release
 * Translate that to /usr/lib/debug/usr/bin/sha256sum-$version-$release and load
 * it.
 */
static sc_elf_object_t *
sc_elf_object_load_debug_link(sc_elf_object_t *object)
{
	const char *path = object->file.path;
	char *debug_dir = NULL, *debug_path = NULL, *sp;

	if (object->debug_link == NULL || path[0] != '/')
		return NULL;

	asprintf(&debug_dir, "/usr/lib/debug/%s", path);
	if ((sp = strrchr(debug_dir, '/')) != NULL)
		*(++sp) = '\0';

	asprintf(&debug_path, "%s%s", debug_dir, object->debug_link);

	if (access(debug_path, R_OK) == 0) {
		sc_object_reference_t debug_ref = { .path = debug_path };
		object->debug_object = sc_elf_object_load(&debug_ref);
		if (object->debug_object == NULL)
			fprintf(stderr, "%s: failed to load ELF debug symbols from %s\n",
					object->file.path, debug_path);
	}
	free(debug_dir);
	free(debug_path);

	return object->debug_object;
}

/*
 * We cache the ELF information we loaded. Useful if we need to process large quantities
 * of coverage files, esp when measure_all is set.
 */
static sc_elf_object_t *
sc_elf_get_object_cached(const sc_object_entry_t *entry)
{
	sc_elf_object_t *object;

	for (object = sc_elf_object_cache; object; object = object->next) {
		if (sc_object_reference_same(&object->file, &entry->file))
			return object;
	}

	object = sc_elf_object_load(&entry->file);
	if (object == NULL)
		return NULL;

	if (object->debug_link)
		sc_elf_object_load_debug_link(object);

	object->next = sc_elf_object_cache;
	sc_elf_object_cache = object;
	object->in_cache = true;

	return object;
}

static void
sc_elf_object_extract_symbols(const sc_elf_object_t *object, sc_coverage_t *coverage, const char *section_name)
{
	sc_elf_section_t *section;
	sc_elf_symbol_t *elfsym;
	unsigned long reloc;
	unsigned int i;

	if (!(section = sc_elf_object_get_section(object, section_name)))
		return;

	reloc = section->hdr.sh_addr - section->hdr.sh_offset;

	for (i = 0, elfsym = object->symbol; i < object->nsymbols; ++i, ++elfsym) {
		if (elfsym->sym.st_shndx == section->index)
			sc_coverage_add_symbol(coverage, elfsym->name, elfsym->sym.st_value - reloc, elfsym->sym.st_size);
	}

	coverage->text_reloc = reloc;
	coverage->text_offset = section->hdr.sh_offset;
	coverage->text_size = section->hdr.sh_size;
}

void
sc_elf_extract_symbols(const sc_object_entry_t *entry, sc_coverage_t *coverage)
{
	sc_elf_object_t *object;

	object = sc_elf_get_object_cached(entry);
	if (object == NULL)
		return;

	sc_elf_object_extract_symbols(object, coverage, ".text");
	if (object->debug_object)
		sc_elf_object_extract_symbols(object->debug_object, coverage, ".text");
}

const sc_symbol_t *
sc_elf_locate_symbol(const sc_object_entry_t *entry, const char *name)
{
	sc_elf_object_t *object;
	sc_elf_section_t *section;
	sc_elf_symbol_t *elfsym;
	unsigned long reloc;
	unsigned int i;

	object = sc_elf_get_object_cached(entry);
	if (object == NULL)
		return NULL;

	if (!(section = sc_elf_object_get_section(object, ".text")))
		return NULL;

	reloc = section->hdr.sh_addr - section->hdr.sh_offset;

	for (i = 0, elfsym = object->symbol; i < object->nsymbols; ++i, ++elfsym) {
		if (elfsym->sym.st_shndx == section->index
		 && !strcmp(elfsym->name, name)) {
			static sc_symbol_t result;

			result.name = name;
			result.start_offset = elfsym->sym.st_value - reloc;
			result.end_offset = result.start_offset + elfsym->sym.st_size;
			return &result;
		}
	}

	return NULL;
}
