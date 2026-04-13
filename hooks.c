/*
 * Various hooks that let us enable sampling as needed/requests.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>

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
	int do_all;

	/* Set the control path from environment (if present); else use
	 * default path. */
	sc_control_set_path(NULL);

	sc_tracing = sc_getenv_int("BLANKET_TRACE");
	do_all = sc_getenv_int("BLANKET_MEASURE_ALL");

	if (sc_tracing)
		printf("Hook called before main()\n");

	if (do_all) {
		ctl = sc_control_create();
		ctl->measure_all = 1;
	} else if (!(ctl = sc_control_read())) {
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

/*
 * Intercept calls to pthread_create
 *
 * Right now, this will work *only* if we already decided to enable sampling before
 * the process calls pthread_create(). In most cases, this will be fine, but things
 * may be different when the application uses dlopen(), and we only discover during
 * dlopen that we should enable PC sampling.
 * Ignore this case for now...
 */

struct sc_pthread_args {
	void *		(*start_routine)(void *);
	void *		arg;
};

static void *
start_thread_with_sampling(void *arg)
{
	struct sc_pthread_args *pargs = arg;
	void *r;

	/* If sampling is enabled, set up signal handler and timer for this new thread, too */
	sc_sampling_activate_thread();

	r = pargs->start_routine(pargs->arg);
	free(pargs);
	return r;
}

int
pthread_create(pthread_t *restrict thread,
		  const pthread_attr_t *attr,
		  void *(*start_routine)(void *),
		  void *arg)
{
	static int	(*__real_pthread_create)(pthread_t *restrict thread,
				const pthread_attr_t *attr,
				void *(*start_routine)(void *),
				void *arg);
	struct sc_pthread_args *pargs;

	/* should we report an error in case dlsym() fails? */
	if (__real_pthread_create == NULL) {
		__real_pthread_create = dlsym(RTLD_NEXT, "pthread_create");
		if (__real_pthread_create == NULL)
			perror("dlsym(RTLD_NEXT, 'pthread_create')");
		else if (__real_pthread_create == pthread_create) {
			fprintf(stderr, "dlsym(RTLD_NEXT, 'pthread_create') returns hook address");
			__real_pthread_create = NULL;
		}
	}

	/* The args struct that we pass to __real_pthread_create() must refer to memory that
	 * remains valid after we return from this function (because the spawned thread needs
	 * to access it). Hence, stack variables are not an option.
	 */
	pargs = calloc(1, sizeof(*pargs));
	pargs->start_routine = start_routine;
	pargs->arg = arg;

	return __real_pthread_create(thread, attr, start_thread_with_sampling, pargs);
}

