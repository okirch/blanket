/*
 * Control blanket operations
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blanket.h"

static void		do_init(void);
static void		do_update(void);
static void		do_add(int nfiles, char **files);
static void		do_show(void);
static void		do_report(int nfiles, char **files);
static const char *	require_arg(const char *, int argc, char **argv);
static void		usage(int);

static const char *	opt_control_path = NULL;
static int		opt_granularity = -1;
static int		opt_test_id = -1;
static int		opt_measure_all = -1;
static int		opt_symbols = -1;

enum {
	OPT_SYMBOLS, OPT_NO_SYMBOLS,
};

static struct option	long_options[] = {
	{ "control-path",	required_argument,	NULL,	'P' },
	{ "measure-all",	no_argument,		NULL,	'A' },
	{ "granularity",	required_argument,	NULL,	'G' },
	{ "symbols",		no_argument,		NULL,	OPT_SYMBOLS },
	{ "no-symbols",		no_argument,		NULL,	OPT_NO_SYMBOLS },
	{ "test-id",		required_argument,	NULL,	'T' },
	{ "help",		no_argument,		NULL,	'h' },
	{ NULL }
};

int
main(int argc, char **argv)
{
	const char *cmd;
	int c;

	while ((c = getopt_long(argc, argv, "AG:T:hp:", long_options, NULL)) > 0) {
		switch (c) {
		case 'A':
			opt_measure_all = 1;
			break;

		case 'G':
			opt_granularity = strtoul(optarg, NULL, 0);
			if (opt_granularity & (opt_granularity - 1)) {
				fprintf(stderr, "Granularity must be a power of 2.\n");
				usage(1);
			}
			break;

		case OPT_SYMBOLS:
			opt_symbols = 1;
			break;

		case OPT_NO_SYMBOLS:
			opt_symbols = 0;
			break;

		case 'T':
			opt_test_id = strtoul(optarg, NULL, 0);
			break;

		case 'p':
			opt_control_path = optarg;
			break;

		case 'h':
			usage(0);
		}
	}

	sc_control_set_path(opt_control_path);
	printf("Using control file %s\n", sc_control_path);

	cmd = require_arg("command", argc, argv);
	if (!strcmp(cmd, "init")) {
		do_init();
	} else if (!strcmp(cmd, "update")) {
		do_update();
	} else if (!strcmp(cmd, "add")) {
		do_add(argc - optind, argv + optind);
	} else if (!strcmp(cmd, "show")) {
		do_show();
	} else if (!strcmp(cmd, "report")) {
		do_report(argc - optind, argv + optind);
	} else {
		fprintf(stderr, "Unsupported command \"%s\"\n", cmd);
		usage(1);
	}

	return 0;
}

/*
 * Update from options
 */
static void
update_and_write_control(sc_control_t *ctl)
{
	if (opt_granularity >= 0)
		ctl->granularity = opt_granularity;
	if (opt_measure_all >= 0)
		ctl->measure_all = opt_measure_all;
	if (opt_test_id >= 0)
		ctl->test_id = opt_test_id;

	if (sc_control_write(ctl) < 0)
		exit(1);
}

static void
do_init(void)
{
	sc_control_t *ctl;

	ctl = sc_control_create();
	update_and_write_control(ctl);
}

void
do_update(void)
{
	sc_control_t *ctl;

	if (!(ctl = sc_control_read())) {
		fprintf(stderr, "Unable to read control file\n");
		exit(1);
	}

	update_and_write_control(ctl);
}

static void
do_add(int nfiles, char **files)
{
	sc_control_t *ctl;
	int i, okay = 1;

	if (!(ctl = sc_control_read())) {
		fprintf(stderr, "Unable to read control file\n");
		exit(1);
	}

	for (i = 0; i < nfiles; ++i) {
		const char *path = files[i];

		if (sc_control_add_file(ctl, path) < 0) {
			okay = 0;
			continue;
		}
	}

	if (!okay)
		exit(1);

	update_and_write_control(ctl);
}

void
do_show(void)
{
	sc_control_t *ctl;
	unsigned int i;

	if (!(ctl = sc_control_read())) {
		fprintf(stderr, "Unable to read control file\n");
		exit(1);
	}

	printf("Test ID:              %2u\n", ctl->test_id);
	printf("Address granularity:  %2u\n", ctl->granularity);
	printf("Address shift:        %2u%s\n", ctl->addr_shift,
					(ctl->granularity == (1 << ctl->addr_shift)? "": " (Should be log2(granularity)!!!)"));
	printf("Measure all:          %s\n",
					(ctl->measure_all)? "yes" : "no");
	if (ctl->num_entries == 0) {
		printf("No object entries\n");
	} else {
		printf("%u object entries\n", ctl->num_entries);
		for (i = 0; i < ctl->num_entries; ++i) {
			sc_control_entry_t *entry = &ctl->entry[i];

			printf("Entry %2u:    dev %04x ino %08lu\n", i, (unsigned int) entry->dev, (unsigned long) entry->ino);
		}
	}
}

static int
show_one_report(const char *path)
{
	sc_object_entry_t *entry;
	sc_coverage_t *coverage;

	entry = sc_object_entry_load(path);
	if (entry == NULL) {
		fprintf(stderr, "Could not load %s\n", path);
		return -1;
	}

	coverage = sc_coverage_extract(entry);
	if (coverage == NULL)
		return -1;

	printf("%s\n", entry->path);
	printf("Text:             %08lx-%08lx\n",
			coverage->text_offset,
			coverage->text_offset + coverage->text_size);
	printf("Test ID:          %5u\n", entry->test_id);
	printf("Sampling size:    %5u\n", 1 << entry->addr_shift);
	printf("Global coverage: %5.2f%%\n", coverage->global_coverage);

	if (opt_symbols) {
		unsigned int i;

		for (i = 0; i < coverage->nsymbols; ++i) {
			sc_symbol_t *sym = &coverage->symbol[i];

			if (sym->num_hits)
				printf("  %5.1f%% %s\n", sym->coverage, sym->name);
		}

		if (coverage->unknown_symbol.num_hits)
			printf("  %6u hits in code without symbol\n", coverage->unknown_symbol.num_hits);
	}

	printf("\n");

	sc_coverage_free(coverage);
	return 0;
}


static void
do_report(int nfiles, char **files)
{
	int i, okay = 1;

	for (i = 0; i < nfiles; ++i) {
		const char *path = files[i];

		if (show_one_report(path) < 0)
			okay = 0;
	}

	if (!okay)
		exit(1);
}

const char *
require_arg(const char *what, int argc, char **argv)
{
	if (optind >= argc) {
		fprintf(stderr, "Missing \"%s\" argument.\n", what);
		usage(1);
	}

	return argv[optind++];
}

void
usage(int ex)
{
	fprintf(stderr,
		"Usage: blanket [options] cmd ...\n"
		"\n"
		"Common options:\n"
		"  --help, -h\n"
		"	Display this message\n"
		"\n"
		"blanket (init|update)\n"
		"	Initialize or update the control file.\n"
		"Options:\n"
		"  --control-path <path>\n"
		"	The control file to use (default: none)\n"
		"  --granularity N\n"
		"	Sampling granularity\n"
		"  --measure-all\n"
		"	Measure all ELF objects\n"
		"  --test-id <ID>\n"
		"	An identifier that will be recorded, allowing to correlate coverage reports with test cases.\n"
		"	For now, must be an integer (but will change to a string at some point).\n"
		"\n"
		"blanket add <path> ...\n"
		"	Specify one or more ELF objects to measure.\n"
		"	The <path> argument can be ELF binaries or shared libraries.\n"
		"	Repeated additions accumulate; use 'init' to reset the list of tracked binaries.\n"
		"	Currently, the implementation is limited to 128 objects (unless you use --measure-all).\n"
		"  --control-path <path>\n"
		"	The control file to use (default: none)\n"
		"\n"
		"blanket report <path> ...\n"
		"	Display stats extracted from coverage files generated by libblanket.so\n"
		"Options:\n"
		"  --symbols\n"
		"	Display per-function coverage. This is the default behavior.\n"
		"	The binary should be either unstripped, or you should have\n"
		"	the corresponding debuginfo package installed.\n"
		"  --no-symbols\n"
		"	Do not display per-function coverage.\n"
	);
	exit(ex);
}
