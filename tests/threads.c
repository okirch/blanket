#include <sys/time.h>
#include <sys/types.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>

static void *
function1(void *dummy)
{
	unsigned long l = 1;

	while (1)
		l *= 7;
	return (void *) l;
}

static void *
function2(void *dummy)
{
	unsigned long l = 3;

	while (1)
		l *= 7;
	return (void *) l;
}

int
main(void)
{
	pthread_attr_t	tattr;
	pthread_t	t1, t2;
	int		r;

	if (pthread_attr_init(&tattr)) {
		perror("pthread_attr_init()");
		return 1;
	}

	printf("About to create threads\n");
	if ((r = pthread_create(&t1, &tattr, function1, NULL)) != 0) {
		fprintf(stderr, "Failed to create thread 1\n");
		return 1;
	}

	if ((r = pthread_create(&t2, &tattr, function2, NULL)) != 0) {
		fprintf(stderr, "Failed to create thread 1\n");
		return 1;
	}

	printf("Giving threads some time to do their job\n");
	{
		struct timeval now, until;

		gettimeofday(&until, NULL);
		until.tv_sec += 1;

		while (1) {
			usleep(100);
			gettimeofday(&now, NULL);
			if (timercmp(&now, &until, >=))
				break;
		}
	}

	pthread_cancel(t1);
	pthread_cancel(t2);
	printf("Done.\n");
#if 1
	while (sleep(1))
		;
#endif
	return 0;
}
