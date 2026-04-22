/*
 * Location functions using DWARF information
 */

#include <elfutils/libdw.h>
#include <fcntl.h>
#include <dwarf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "blanket.h"

struct sc_dwarf_searchctx {
	char *			filename;
	char *			function;

	int			file_index;
	Dwarf_Addr		start_addr, end_addr;
};

typedef struct sc_dwarf_cu_ctx {
	uint8_t			unit_num;
	uint8_t			unit_type;

	Dwarf_Die		die;
	Dwarf_Files *		files;
	size_t			nfiles;
	Dwarf_Lines *		lines;
	size_t			nlines;
} sc_dwarf_cu_ctx_t;

typedef struct sc_dwarf_iterator {
	int			fd;
	Dwarf *			dbg;
	Dwarf_CU *		cu;
	unsigned int		cu_num;

	sc_dwarf_cu_ctx_t *	cu_ctx;
} sc_dwarf_iterator_t;

typedef struct {
	const char *		name;
	unsigned long		start_offset, end_offset;
	int			file;
} sc_dwarf_symbol_t;

static void
sc_dwarf_cu_ctx_free(sc_dwarf_cu_ctx_t *ctx)
{
	free(ctx);
}

static inline void
sc_dwarf_iterator_drop_cu(sc_dwarf_iterator_t *iter)
{
	if (iter->cu_ctx != NULL)
		sc_dwarf_cu_ctx_free(iter->cu_ctx);
	iter->cu_ctx = NULL;
}

static void
sc_dwarf_iterator_free(sc_dwarf_iterator_t *iter)
{
	if (iter->dbg != NULL)
		dwarf_end(iter->dbg);
	if (iter->fd >= 0)
		close(iter->fd);
	free(iter);
}

sc_dwarf_iterator_t *
sc_dwarf_open(const char *path)
{
	sc_dwarf_iterator_t *iter;

	iter = calloc(1, sizeof(*iter));
	iter->fd = -1;

	if ((iter->fd = open(path, O_RDONLY)) < 0) {
		fprintf(stderr, "Cannot open %s: %m\n", path);
		goto failed;
	}

	if ((iter->dbg = dwarf_begin(iter->fd, DWARF_C_READ)) == NULL) {
		fprintf(stderr, "Cannot obtain dwarf handle for %s: maybe stripped?\n", path);
		goto failed;
	}

	return iter;

failed:
	sc_dwarf_iterator_free(iter);
	return NULL;
}

static sc_dwarf_cu_ctx_t *
sc_dwarf_next_cu(sc_dwarf_iterator_t *iter)
{
	sc_dwarf_cu_ctx_t *ctx;

	sc_dwarf_iterator_drop_cu(iter);

	ctx = calloc(1, sizeof(*ctx));

	if (dwarf_get_units(iter->dbg, iter->cu, &iter->cu, 0, 0, 0, 0) != 0)
		goto failed;

	if (dwarf_cu_die(iter->cu, &ctx->die, NULL, NULL, NULL, NULL, NULL, NULL) == NULL)
		goto failed;

	if (dwarf_cu_info(iter->cu, NULL, &ctx->unit_type, NULL, NULL, NULL, NULL, NULL) != 0)
		goto failed;

	if (dwarf_getsrcfiles(&ctx->die, &ctx->files, &ctx->nfiles) != 0)
		ctx->nfiles = 0;

	if (dwarf_getsrclines(&ctx->die, &ctx->lines, &ctx->nlines) != 0)
		ctx->nlines = 0;

	iter->cu_ctx = ctx;
	ctx->unit_num = ++(iter->cu_num);
	return ctx;

failed:
	sc_dwarf_cu_ctx_free(ctx);
	return NULL;
}

static sc_source_file_t *
sc_dwarf_cu_get_source_file(sc_dwarf_cu_ctx_t *cu, const sc_dwarf_symbol_t *sym, sc_coverage_t *coverage)
{
	unsigned int index = sym->file;
	const char *path = NULL;
	sc_source_file_t *file;

	if (index == 0 || index >= cu->nfiles)
		return NULL;

	path = dwarf_filesrc(cu->files, index, NULL, NULL);
	file = sc_coverage_add_source_file(coverage, cu->unit_num, path);
	if (file != NULL)
		file->file_id = index;
	return file;
}

static const sc_dwarf_symbol_t *
sc_dwarf_cu_find_function(sc_dwarf_cu_ctx_t *cu, unsigned long addr)
{
	Dwarf_Die child;
	Dwarf_Attribute attr;
	Dwarf_Addr low_pc;
	Dwarf_Word high_pc, file_index;
	int s;

	for (s = dwarf_child(&cu->die, &child); s == 0; s = dwarf_siblingof(&child, &child)) {
		const char *name = dwarf_diename(&child);

		if (name == NULL)
			continue;

		if (dwarf_formaddr(dwarf_attr(&child, DW_AT_low_pc, &attr), &low_pc) != 0
		 || dwarf_formudata(dwarf_attr(&child, DW_AT_high_pc, &attr), &high_pc) != 0
		 || dwarf_formudata(dwarf_attr(&child, DW_AT_decl_file, &attr), &file_index) != 0)
			continue;

		if (low_pc <= addr && addr - low_pc < high_pc) {
			static sc_dwarf_symbol_t sym;

			sym.name = name;
			sym.start_offset = low_pc;
			sym.end_offset = low_pc + high_pc;
			sym.file = file_index;
			return &sym;
		}
	}

	return NULL;
}

void
sc_dwarf_extract_coverage(const sc_object_entry_t *entry, sc_coverage_t *coverage)
{
	sc_dwarf_iterator_t *iter;
	sc_dwarf_cu_ctx_t *cu;

	if ((iter = sc_dwarf_open(entry->path)) == NULL)
		return;

	while ((cu = sc_dwarf_next_cu(iter)) != NULL) {
		unsigned long addr = 0;
		sc_source_file_t *source_file = NULL;

		addr = sc_object_entry_get_next_hit(entry, 0, coverage->text_reloc);
		while (addr) {
			const sc_dwarf_symbol_t *symbol;

			symbol = sc_dwarf_cu_find_function(cu, addr);
			if (symbol == NULL) {
				addr = sc_object_entry_get_next_hit(entry, addr, coverage->text_reloc);
				continue;
			}

			if (source_file == NULL || source_file->file_id != symbol->file)
				source_file = sc_dwarf_cu_get_source_file(cu, symbol, coverage);

			while (addr && symbol->start_offset <= addr && addr < symbol->end_offset) {
				Dwarf_Line *dl;
				int line;

				if (source_file
				 && (dl = dwarf_getsrc_die(&cu->die, (Dwarf_Addr) addr)) != NULL
				 && dwarf_lineno(dl, &line) == 0)
					sc_source_file_add_line_hit(source_file, line);

				addr = sc_object_entry_get_next_hit(entry, addr, coverage->text_reloc);
			}
		}
	}

	sc_dwarf_iterator_free(iter);
}

/*******************************************************************
 * DWARF dumping to help me with understanding what's going on.
 *******************************************************************/
static const char *	attr_strings[0x100] = {
#define _(n)	[DW_AT_##n] = #n
	_(sibling),
	_(location),
	_(name),
	_(ordering),
	_(byte_size),

	/* 0x10 */
	_(stmt_list),
	_(low_pc),
	_(high_pc),
	_(language),
	_(comp_dir),


	/* 0x20 */
	_(inline),
	_(is_optional),
	_(lower_bound),
	_(producer),
	_(prototyped),
	_(return_addr),
	_(upper_bound),

	/* 0x30 */
	_(data_member_location),
	_(decl_column),
	_(decl_file),
	_(decl_line),
	_(external),
	_(declaration),
	_(encoding),

	/* 0x40 */
	_(frame_base),
	_(type),

	/* 0x50 */
	_(ranges),

	/* 0x80 */
	_(alignment),
#undef _
};

static const char *
sc_dwarf_render_attr_code(unsigned int code)
{
	static char buffer[16];
	const char *s;

	if (code < 0x100 && (s = attr_strings[code]) != NULL)
		return s;

	snprintf(buffer, sizeof(buffer), "0x%x", code);
	return buffer;
}

static const char *
sc_dwarf_format_value(Dwarf_Attribute *attr)
{
	static char buffer[32];
	unsigned int form = dwarf_whatform(attr);
        Dwarf_Addr addr;
        Dwarf_Word val;
	Dwarf_Block block;
	Dwarf_Die ref;
	bool bval;

	switch (form) {
	case DW_FORM_strp:
	case DW_FORM_string:
		return dwarf_formstring(attr);

	case DW_FORM_addr:
		if (dwarf_formaddr(attr, &addr) != 0)
			goto failed;
		snprintf(buffer, sizeof(buffer), "addr=%p", (caddr_t) addr);
		break;

	case DW_FORM_data1:
	case DW_FORM_data2:
	case DW_FORM_data4:
	case DW_FORM_data8:
	case DW_FORM_udata:
	case DW_FORM_sec_offset:
		if (dwarf_formudata(attr, &val) != 0)
			goto failed;
		snprintf(buffer, sizeof(buffer), "data=%lu", (long) val);
		break;

	case DW_FORM_flag_present:
		if (dwarf_formflag(attr, &bval) != 0)
			goto failed;
		if (bval)
			return "true";
		return "false";

	case DW_FORM_block:
	case DW_FORM_block2:
	case DW_FORM_block4:
	case DW_FORM_exprloc:
		if (dwarf_formblock(attr, &block) != 0)
			goto failed;
		snprintf(buffer, sizeof(buffer), "block=%p+%lu", block.data, block.length);
		break;

	case DW_FORM_ref1:
	case DW_FORM_ref2:
	case DW_FORM_ref4:
	case DW_FORM_ref8:
	case DW_FORM_ref_udata:
		if (dwarf_formref_die(attr, &ref) == NULL)
			goto failed;
		snprintf(buffer, sizeof(buffer), "dieref=%lu (%s)",
				dwarf_dieoffset(&ref),
				dwarf_diename(&ref));
		break;

	default:
	failed:
		snprintf(buffer, sizeof(buffer), "form 0x%x", form);
		break;
	}
	return buffer;
}

static int
sc_dwarf_attr_show(Dwarf_Attribute *attr, void *arg)
{
	unsigned int indent = *(unsigned int *) arg;
	unsigned int code = dwarf_whatattr(attr);

	if (code != DW_AT_sibling) {
		printf("%*.*s%-12s %s (form 0x%x)\n",
				indent, indent, "",
				sc_dwarf_render_attr_code(code),
				sc_dwarf_format_value(attr),
				dwarf_whatform(attr));
	}
	return DWARF_CB_OK;
}

static void
sc_dwarf_die_show(Dwarf_Die *die, unsigned int indent)
{
	Dwarf_Die child;

	dwarf_getattrs(die, sc_dwarf_attr_show, &indent, 0);

	if (dwarf_child(die, &child) != 0)
		return;

	do {
		printf("%*.*s%s (%lu)\n",
				indent + 2, indent + 2, "",
				dwarf_diename(&child),
				dwarf_dieoffset(&child));
		sc_dwarf_die_show(&child, indent + 4);
	} while (dwarf_siblingof(&child, &child) == 0);
}

void
sc_dwarf_dump(const char *path)
{
	sc_dwarf_iterator_t *iter;
	sc_dwarf_cu_ctx_t *cu;

	if ((iter = sc_dwarf_open(path)) == NULL)
		return;

	while ((cu = sc_dwarf_next_cu(iter)) != NULL) {
		unsigned int i;

		printf("CU #%u type %d %s\n", cu->unit_num, cu->unit_type, dwarf_diename(&cu->die));
		for (i = 1; i < cu->nfiles; ++i)
			printf("  file %u %s\n", i, dwarf_filesrc(cu->files, i, NULL, NULL));

		sc_dwarf_die_show(&cu->die, 4);
	}

	sc_dwarf_iterator_free(iter);
}
