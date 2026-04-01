
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "blanket.h"

int	sc_tracing = 0;

/*
 * Extract int value from env var
 */
static int
sc_getenv_int(const char *name)
{
	const char *value;

	if ((value = getenv(name)) == NULL)
		return 0;
	return strtoul(value, NULL, 0);
}

/*
 * Callback invoked before main()
 * This captures the list of libs loaded into the process by ld.so
 */
__attribute__((constructor))
static void
pre_main_hook(void)
{
	sc_control_t *ctl;
	const char *path;
	int do_all;

	sc_tracing = sc_getenv_int("BLANKET_TRACE");
	do_all = sc_getenv_int("BLANKET_MEASURE_ALL");
	path = getenv("BLANKET_CONTROL");

	if (sc_tracing)
		printf("Hook called before main()\n");

	if (do_all) {
		ctl = sc_control_create();
		ctl->measure_all = 1;
	} else if (!(ctl = sc_control_read(path))) {
		return;
	}

	sc_context_init(ctl);

	if (sc_context_rescan())
		sc_sampling_enable();
}

/*
 * Intercept calls to dlopen()
 */
void *
dlopen(const char *path, int flags)
{
	void *h;

	h = dlmopen(LM_ID_NEWLM, path, flags);

	if (sc_tracing)
		printf("Loaded %s\n", path);

	if (sc_context_rescan())
		sc_sampling_enable();

	return h;
}
