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

#include "blanket.h"

static int		sc_sampling_enabled = 0;

static void		sc_sampling_interrupt(int, siginfo_t *, void *);


int
sc_sampling_enable(void)
{
	sigset_t sigset;
	struct sigaction act;
	struct itimerval itimer;

	if (sc_context == NULL)
		return -1;

	if (sc_sampling_enabled)
		return 0;

	sc_sampling_enabled = 1;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGALRM);
	sigaddset(&sigset, SIGVTALRM);
	sigprocmask(SIG_UNBLOCK, &sigset, NULL);

	memset(&act, 0, sizeof(act));
	act.sa_sigaction = sc_sampling_interrupt;
	act.sa_flags = SA_SIGINFO | SA_RESTART;
	sigaction(SIGALRM, &act, NULL);

	memset(&itimer, 0, sizeof(itimer));
	itimer.it_interval.tv_usec = 1;
	itimer.it_value = itimer.it_interval;

	printf("Enable timer\n");
	setitimer(ITIMER_REAL, &itimer, NULL);
	sleep(1);

	return 0;
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
