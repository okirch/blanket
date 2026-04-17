/*
 * This is a small test program to verify that -pg/mcount capturing works.
 */

#include <stdio.h>
#include <unistd.h>
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
	double accum;

	for (accum = 0; (accum += elapsed()) < 2; ) {
		usleep(1000);
	}

	return (accum == 0);
}
