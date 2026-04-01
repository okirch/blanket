/*
 * Control blanket operations
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blanket.h"

static void		do_init();
static void		do_update();
static void		do_add(int nfiles, char **files);
static const char *	require_arg(const char *, int argc, char **argv);
static void		usage(int);

static const char *	opt_control_path = NULL;
static int		opt_granularity = -1;
static int		opt_test_id = -1;
static int		opt_measure_all = -1;

int
main(int argc, char **argv)
{
	const char *cmd;
	int c;

	while ((c = getopt(argc, argv, "AG:T:hp:")) > 0) {
		switch (c) {
		case 'A':
			opt_granularity = 1;
			break;

		case 'G':
			opt_granularity = strtoul(optarg, NULL, 0);
			if (opt_granularity & (opt_granularity - 1)) {
				fprintf(stderr, "Granularity must be a power of 2.\n");
				usage(1);
			}
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

	cmd = require_arg("command", argc, argv);
	if (!strcmp(cmd, "init")) {
		do_init();
	} else if (!strcmp(cmd, "update")) {
		do_update();
	} else if (!strcmp(cmd, "add")) {
		do_add(argc - optind, argv + optind);
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

	if (sc_control_write(ctl, opt_control_path) < 0)
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

	if (!(ctl = sc_control_read(opt_control_path))) {
		fprintf(stderr, "Could not read control file: %m\n");
		exit(1);
	}

	update_and_write_control(ctl);
}

static void
do_add(int nfiles, char **files)
{
	sc_control_t *ctl;
	int i, okay = 1;

	if (!(ctl = sc_control_read(opt_control_path))) {
		fprintf(stderr, "Could not read control file: %m\n");
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
	fprintf(stderr, "Help message under construction.\n");
	exit(ex);
}
