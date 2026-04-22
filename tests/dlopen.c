/*
 * This is a small test program to verify that we do capture shared libs as they are
 * attached
 */

#include <dlfcn.h>
#include <stdio.h>
#include <time.h>

static double
elapsed(void)
{
	static double t0 = 0;

	if (t0 == 0)
		t0 = time(NULL);
	return time(NULL) - t0;
}

int
main(void)
{
	void *h;
	double (*sqrt)(double);
	unsigned int i, j;
	double accum;

	h = dlopen("/lib64/libm.so.6", RTLD_LAZY);
	if (h == NULL) {
		printf("Could not /lib64/libm.so.6: %s\n", dlerror());
		return 1;
	}

	sqrt = dlsym(h, "sqrt");
	if (sqrt == NULL) {
		printf("Could not find sqrt: %s\n", dlerror());
		return 1;
	}

	for (i = 0, accum = 0; elapsed() < 2; ) {
		for (j = 0; j < 1000; ++i, ++j)
			accum += sqrt((double) i);
	}

	printf("Performed %u sqrt calls\n", i);
	return (accum == 0);
}
