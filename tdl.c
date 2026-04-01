/*
 * This is a small test program to verify that we do capture shared libs as they are
 * attached
 */

#include <dlfcn.h>
#include <stdio.h>

int main(void)
{
	void *h;
	double (*sqrt)(double);

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

	printf("sqrt(4)=%f\n", sqrt(4));
	return 0;
}
