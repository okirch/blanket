/*
 * Handle instruction counter sampling
 *
 * This obviously needs work
 */

#include <sys/time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "blanket.h"

static int		sc_sampling_enabled = -1;
static __thread int	sc_sampling_active_for_thread;
static __thread	timer_t	sc_thread_timer;

static void		sc_sampling_interrupt(int, siginfo_t *, void *);


int
sc_sampling_enable(const sc_context_t *ctx)
{
	if (sc_context == NULL)
		return -1;

	if (sc_sampling_enabled >= 0)
		return 0;

	sc_sampling_enabled = ctx->mode;

	switch (ctx->mode) {
	case SC_MODE_TIMER:
		sc_sampling_activate_thread();
		break;

	case SC_MODE_MCOUNT:
		/* nothing to be done */
		break;

	default:
		/* not implemented */;
	}

	return 0;
}

static void
sc_sampling_set_signal_handler(void)
{
	sigset_t sigset;
	struct sigaction act;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGALRM);
	sigaddset(&sigset, SIGVTALRM);
	sigprocmask(SIG_UNBLOCK, &sigset, NULL);

	memset(&act, 0, sizeof(act));
	act.sa_sigaction = sc_sampling_interrupt;
	act.sa_flags = SA_SIGINFO | SA_RESTART;
	sigaction(SIGVTALRM, &act, NULL);
}

#if 0
static void
sc_sampling_set_timer(void)
{
	struct itimerval itimer;


	memset(&itimer, 0, sizeof(itimer));
	itimer.it_interval.tv_usec = 1;
	itimer.it_value = itimer.it_interval;

	setitimer(ITIMER_VIRTUAL, &itimer, NULL);
}
#else
void
sc_sampling_set_timer(void)
{
	struct itimerspec itimer;
	struct sigevent   event;

	memset(&event, 0, sizeof(event));
	event.sigev_notify = SIGEV_THREAD_ID;

	/* sigev_notify_thread_id is defined in asm/siginfo.h, but
	 * not exposed by glibc. Hack our way around this. */
#ifdef notyet
	event.sigev_notify_thread_id = gettid();
#else
	{
		int *tidp = (int *) & event._sigev_un;
		*tidp = gettid();
	}
#endif

	event.sigev_signo = SIGVTALRM;
	event.sigev_notify_attributes = NULL;
	if (timer_create(CLOCK_THREAD_CPUTIME_ID, &event, &sc_thread_timer) < 0) {
		perror("timer_create");
		return;
	}

	memset(&itimer, 0, sizeof(itimer));
	itimer.it_interval.tv_nsec = 1000;
	itimer.it_value = itimer.it_interval;
	if (timer_settime(sc_thread_timer, 0, &itimer, NULL) < 0) {
		perror("timer_settime");
		timer_delete(&sc_thread_timer);
		return;
	}
}
#endif

void
sc_sampling_activate_thread(void)
{
	if (sc_sampling_enabled < 0) {
		printf("Sampling not enabled\n");
		return;
	}

	if (sc_sampling_active_for_thread)
		return;
	sc_sampling_active_for_thread = 1;

	sc_sampling_set_signal_handler();
	sc_sampling_set_timer();
}

void
sc_sampling_interrupt(int signo, siginfo_t *si, void *uc)
{
	const ucontext_t *uctx = (ucontext_t *) uc;
	caddr_t eip;

	/* now add the sample */
	eip = (caddr_t) uctx->uc_mcontext.gregs[REG_RIP];
	sc_context_add_sample(sc_context, eip);
}
