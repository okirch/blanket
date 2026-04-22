/*
 * Control blanket operations
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "blanket.h"

static void		do_init(void);
static void		do_update(void);
static void		do_add(int nfiles, char **files);
static void		do_trace(int argc, char **argv);
static void		do_show(void);
static void		do_report(int nfiles, char **files);
static const char *	require_arg(const char *, int argc, char **argv);
static void		usage(int);

static const char *	opt_control_path = NULL;
static long		opt_sampling_interval = -1;
static int		opt_granularity = -1;
static const char *	opt_test_id = NULL;
static int		opt_measure_all = -1;
static int		opt_mode = -1;

static int		opt_details = 0;

enum {
	OPT_SYMBOLS, OPT_SOURCELINES, OPT_ANNOTATE, OPT_NO_DETAILS,
};

static struct option	long_options[] = {
	{ "control-path",	required_argument,	NULL,	'P' },
	{ "measure-all",	no_argument,		NULL,	'A' },
	{ "mode",		required_argument,	NULL,	'M' },
	{ "granularity",	required_argument,	NULL,	'G' },
	{ "sampling-interval",	required_argument,	NULL,	'S' },
	{ "symbols",		no_argument,		NULL,	OPT_SYMBOLS },
	{ "sourcelines",	no_argument,		NULL,	OPT_SOURCELINES },
	{ "annotate",		no_argument,		NULL,	OPT_ANNOTATE },
	{ "no-details",		no_argument,		NULL,	OPT_NO_DETAILS },
	{ "test-id",		required_argument,	NULL,	'T' },
	{ "help",		no_argument,		NULL,	'h' },
	{ NULL }
};

int
main(int argc, char **argv)
{
	const char *cmd;
	int c;

	while ((c = getopt_long(argc, argv, "AG:M:S:T:hp:", long_options, NULL)) > 0) {
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

		case 'M':
			if (!sc_string_to_mode(optarg, &opt_mode)) {
				fprintf(stderr, "Invalid sampling mode \"%s\"\n", optarg);
				usage(1);
			}
			break;

		case OPT_SYMBOLS:
			opt_details |= SC_DETAIL_SYMBOLS;
			break;

		case OPT_SOURCELINES:
			opt_details |= SC_DETAIL_SOURCELINES;
			break;

		case OPT_ANNOTATE:
			opt_details |= SC_DETAIL_SOURCELINES | SC_DETAIL_ANNOTATE;
			break;

		case OPT_NO_DETAILS:
			opt_details = SC_DETAIL_NONE;
			break;

		case 'S':
			opt_sampling_interval = strtol(optarg, NULL, 0);
			if (opt_sampling_interval <= 0) {
				fprintf(stderr, "Invalid sampling interval \"%s\"\n", optarg);
				usage(1);
			}
			if (opt_sampling_interval >= 1000000000)
				fprintf(stderr, "Capped sampling interval \"%s\" to %ld\n", optarg, opt_sampling_interval);
			break;

		case 'T':
			if (strlen(optarg) > SC_MAX_TEST_ID)
				fprintf(stderr, "test id \"%s\" too long, will be truncated to %.*s\n", optarg, SC_MAX_TEST_ID, optarg);
			opt_test_id = optarg;
			break;

		case 'P':
			opt_control_path = optarg;
			break;

		case 'h':
			usage(0);

		default:
			usage(1);
		}
	}

	sc_control_set_path(opt_control_path);
	printf("Using control file %s\n", sc_control_path);

	if (opt_details == 0)
		opt_details = SC_DETAIL_SYMBOLS;

	cmd = require_arg("command", argc, argv);
	if (!strcmp(cmd, "init")) {
		do_init();
	} else if (!strcmp(cmd, "update")) {
		do_update();
	} else if (!strcmp(cmd, "add")) {
		do_add(argc - optind, argv + optind);
	} else if (!strcmp(cmd, "trace")) {
		do_trace(argc - optind, argv + optind);
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
	if (opt_mode >= 0)
		ctl->mode = opt_mode;
	if (opt_sampling_interval >= 0)
		ctl->sampling_interval = opt_sampling_interval;
	if (opt_granularity >= 0) {
		ctl->granularity = opt_granularity;
		ctl->addr_shift = ffsl(opt_granularity) - 1;
	}
	if (opt_measure_all >= 0)
		ctl->measure_all = opt_measure_all;
	if (opt_test_id != NULL) {
		strncpy(ctl->test_id, opt_test_id, SC_MAX_TEST_ID);
		ctl->test_id[SC_MAX_TEST_ID] = '\0';
	}

	if (ctl->mode == SC_MODE_PTRACE) {
		if (ctl->granularity != 1) {
			printf("Defaulting granularity=1 for ptrace mode\n");
			ctl->granularity = 1;
			ctl->addr_shift = 0;
		}
		if (ctl->measure_all) {
			fprintf(stderr, "mode ptrace and measure_all does not work\n");
			exit(1);
		}
	}

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

	if (ctl->mode == SC_MODE_PTRACE) {
		fprintf(stderr, "Refusing to monitor entire ELF objects in ptrace mode.\n");
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
do_trace(int argc, char **argv)
{
	sc_control_t *ctl;
	int i, okay = 1;
	const char *elf_path;

	if (!(ctl = sc_control_read())) {
		fprintf(stderr, "Unable to read control file\n");
		exit(1);
	}

	if (ctl->mode != SC_MODE_PTRACE) {
		fprintf(stderr, "Mode must be set to ptrace for this.\n");
		exit(1);
	}

	if (argc < 2) {
		fprintf(stderr, "Missing argument(s)\n");
		usage(1);
	}

	elf_path = argv[0];
	if (access(elf_path, R_OK) < 0) {
		fprintf(stderr, "Cannot access ELF object \"%s\": %m\n", elf_path);
		exit(1);
	}

	for (i = 1; i < argc; ++i) {
		const char *symbol_name = argv[i];

		if (sc_control_add_file_symbol(ctl, elf_path, symbol_name) < 0) {
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

	if (ctl->test_id[0])
		printf("Test ID:              %s\n", ctl->test_id);
	printf("Mode:                 %s\n", sc_mode_to_string(ctl->mode));

	switch (ctl->mode) {
	case SC_MODE_TOUCH:
		break;

	case SC_MODE_TIMER:
		printf("Sampling interval:    %d nsec\n", ctl->sampling_interval);
		printf("Address granularity:  %u\n", ctl->granularity);
		printf("Address shift:        %u%s\n", ctl->addr_shift,
					(ctl->granularity == (1 << ctl->addr_shift)? "": " (Should be log2(granularity)!!!)"));
		break;

	case SC_MODE_MCOUNT:
		break;

	case SC_MODE_PTRACE:
		break;

	case SC_MODE_PLT:
		break;

	default:
		printf("Mode:                 unknown (%d)\n", ctl->mode);
		break;
	}

	printf("Measure all:          %s\n",
					(ctl->measure_all)? "yes" : "no");
	printf("\n");

	if (ctl->num_entries == 0) {
		printf("No object entries\n");
	} else {
		printf("%u object entries\n", ctl->num_entries);
		printf("  %4s %12s path\n", "dev", "ino");
		for (i = 0; i < ctl->num_entries; ++i) {
			sc_control_entry_t *entry = &ctl->entry[i];

			printf("  %04x %12lu %s",
					(unsigned int) entry->file.dev, (unsigned long) entry->file.ino,
					entry->file.path);

			if (entry->region_end)
				printf("; region=%08lx-%08lx",
						(long) entry->region_start,
						(long) entry->region_end);
			printf("\n");
		}
	}
}

static void
do_report(int nfiles, char **files)
{
	sc_report_t *report;
	int i, okay = 1;

	report = sc_report_alloc(opt_details);
	for (i = 0; i < nfiles; ++i) {
		const char *path = files[i];

		if (sc_report_process_file(report, path) < 0)
			okay = 0;
	}

	sc_report_trailer(report);

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
		"  --mode <MODE>\n"
		"	Specify how to measure coverage, can be one of timer (for timer based EIP sampling),\n"
		"	mcount (for intercepting the mcount() call for elf binaries compiled with -pg),\n"
		"	ptrace (for single-stepping through a single function), or\n"
		"	plt (for function call interception by patching the PLT; not yet implemented).\n"
		"  --test-id <ID>\n"
		"	An identifier that will be recorded, allowing to correlate coverage reports with test cases.\n"
		"\n"
		"blanket add <path> ...\n"
		"	Specify one or more ELF objects to measure.\n"
		"	The <path> argument can be ELF binaries or shared libraries.\n"
		"	Repeated additions accumulate; use 'init' to reset the list of tracked binaries.\n"
		"	Currently, the implementation is limited to 128 objects (unless you use --measure-all).\n"
		"  --control-path <path>\n"
		"	The control file to use (default: none)\n"
		"blanket trace <path> <symbol> ...\n"
		"	Specify an ELF object to measure, and the list of symbols that are of interest.\n"
		"	Note that this requires mode \"ptrace\".\n"
		"\n"
		"blanket report <path> ...\n"
		"	Display stats extracted from coverage files generated by libblanket.so\n"
		"Options:\n"
		"  --symbols\n"
		"	Display per-function coverage. This is the default behavior.\n"
		"	The binary should be either unstripped, or you should have\n"
		"	the corresponding debuginfo package installed.\n"
		"  --no-details\n"
		"	Do not display any coverage details, just the summary.\n"
	);
	exit(ex);
}
