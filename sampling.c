/*
 * Handle instruction counter sampling
 *
 * This obviously needs work
 */

#include <sys/time.h>
#include <sys/user.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "blanket.h"

static int		sc_sampling_enabled = -1;
static unsigned long	sc_sampling_interval = SC_DEFAULT_SAMPLING_INTERVAL; /* nsec */
static __thread int	sc_sampling_active_for_thread;
static __thread	timer_t	sc_thread_timer;

static void		sc_sampling_interrupt(int, siginfo_t *, void *);
static int		sc_ptrace_start(void);


int
sc_sampling_enable(const sc_context_t *ctx)
{
	if (sc_context == NULL)
		return -1;

	if (sc_sampling_enabled >= 0)
		return 0;

	sc_sampling_enabled = sc_context_get_mode(ctx);

	switch (sc_context_get_mode(ctx)) {
	case SC_MODE_TIMER:
		if ((sc_sampling_interval = sc_context_get_sampling_interval(ctx)) == 0)
			sc_sampling_interval = SC_DEFAULT_SAMPLING_INTERVAL;
		sc_sampling_activate_thread();
		break;

	case SC_MODE_MCOUNT:
		/* nothing to be done */
		break;

	case SC_MODE_PTRACE:
		sc_ptrace_start();
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
sc_sampling_set_timer(unsigned long interval)
{
	struct itimerval itimer;


	memset(&itimer, 0, sizeof(itimer));
	itimer.it_interval.tv_usec = interval / 1000;
	if (itimer.it_interval.tv_usec == 0)
		itimer.it_interval.tv_usec = 1;
	itimer.it_value = itimer.it_interval;

	setitimer(ITIMER_VIRTUAL, &itimer, NULL);
}
#else
void
sc_sampling_set_timer(unsigned long interval)
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
	itimer.it_interval.tv_nsec = interval;
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
	if (sc_sampling_enabled != SC_MODE_TIMER)
		return;

	if (sc_sampling_active_for_thread)
		return;
	sc_sampling_active_for_thread = 1;

	sc_sampling_set_signal_handler();
	sc_sampling_set_timer(sc_sampling_interval);
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

/*
 * ptrace sampling
 */
#include <sys/ptrace.h>
#include <sys/wait.h>

typedef struct sc_ptrace_break {
	unsigned long		address;
	struct sc_ptrace_function *owner;

	unsigned int		num_hits;

	unsigned char		permanent;
	unsigned char		enabled;
	unsigned char		active;
	unsigned char		saved_ins;
} sc_ptrace_break_t;

typedef struct sc_ptrace_function {
	unsigned long		start_addr;
	unsigned long		end_addr;

	unsigned long		stack_on_entry;

	sc_ptrace_break_t *	entry_breakpoint;
	sc_ptrace_break_t *	return_breakpoint;
} sc_ptrace_function_t;

typedef struct sc_ptrace {
	pid_t			child_pid;

	int			debug_enabled;

	unsigned int		word_size;
	unsigned char		break_ins;

	sc_ptrace_break_t *	step_over_break;

	unsigned int		num_functions;
	sc_ptrace_function_t *	functions;

	unsigned int		num_breakpoints;
	sc_ptrace_break_t *	breakpoints;
} sc_ptrace_t;

static inline int
sc_ptrace_debug(const sc_ptrace_t *ctx)
{
	return ctx->debug_enabled;
}

static sc_ptrace_t *
sc_ptrace_get_context(void)
{
	static sc_ptrace_t *	handle = NULL;

	if (handle != NULL)
		return handle;

	handle = calloc(1, sizeof(*handle));

	handle->word_size = sizeof(long);
	handle->break_ins = 0xCC; /* hard-code for now */
	return handle;
}

/*
 * rather than dealing with endianness, copy data between a (long) int and a byte array
 */
static void
sc_ptrace_write_breakpoint(sc_ptrace_t *ctx, sc_ptrace_break_t *bp, unsigned char ins)
{
	unsigned long start_addr, aligned, offset, new_value;
	unsigned char bytes[8];
	long r;

	start_addr = bp->address;
	aligned = start_addr & ~(ctx->word_size - 1);
	offset = start_addr - aligned;
	/* assert(offset < ctx->word_size); */

	r = ptrace(PTRACE_PEEKTEXT, ctx->child_pid, aligned, bytes);
	memcpy(bytes, &r, sizeof(r));

	if (sc_ptrace_debug(ctx))
		printf("%lx PEEKTEXT bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n", aligned,
			bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);

	if (bytes[offset] == ins)
		return;

	bp->saved_ins = bytes[offset];

	bytes[offset] = ins;
	memcpy(&new_value, bytes, ctx->word_size);

	r = ptrace(PTRACE_POKETEXT, ctx->child_pid, aligned, new_value);

	if (sc_ptrace_debug(ctx))
		printf("%lx POKETEXT bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n", aligned,
			bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);
}

static void
sc_ptrace_enable_breakpoint(sc_ptrace_t *ctx, sc_ptrace_break_t *bp)
{
	if (bp->enabled && !bp->active) {
		sc_ptrace_write_breakpoint(ctx, bp, ctx->break_ins);
		bp->active = 1;
	}
}

static void
sc_ptrace_enable_temporary_breakpoint(sc_ptrace_t *ctx, sc_ptrace_break_t *bp, unsigned long rip)
{
	if (bp->active || bp->permanent)
		return;

	bp->address = rip;
	bp->enabled = 1;

	sc_ptrace_enable_breakpoint(ctx, bp);
}

static void
sc_ptrace_disable_breakpoint(sc_ptrace_t *ctx, sc_ptrace_break_t *bp)
{
	if (bp->active) {
		sc_ptrace_write_breakpoint(ctx, bp, bp->saved_ins);
		bp->active = 0;
	}
}

static void
sc_ptrace_enable_breakpoints(sc_ptrace_t *ctx)
{
	unsigned int i;

	for (i = 0; i < ctx->num_breakpoints; ++i) {
		sc_ptrace_break_t *bp = &ctx->breakpoints[i];

		if (bp->permanent)
			sc_ptrace_enable_breakpoint(ctx, bp);
	}
}

static void
sc_ptrace_detach(sc_ptrace_t *ctx)
{
	if (ctx->child_pid != 0) {
		ptrace(PTRACE_DETACH, ctx->child_pid, 0, 0);
		ctx->child_pid = 0;
	}
}

static sc_ptrace_break_t *
sc_ptrace_create_breakpoint(sc_ptrace_t *ctx, unsigned long addr, int permanent, sc_ptrace_function_t *owner)
{
	sc_ptrace_break_t *bp;

	if ((ctx->num_breakpoints % 8) == 0)
		ctx->breakpoints = realloc(ctx->breakpoints, (ctx->num_breakpoints + 8) * sizeof(ctx->breakpoints[0]));

	bp = &ctx->breakpoints[ctx->num_breakpoints++];
	memset(bp, 0, sizeof(*bp));

	bp->address = addr;
	bp->permanent = permanent;
	bp->enabled = permanent;
	bp->owner = owner;

	if (bp->enabled && addr)
		bp->saved_ins = *(unsigned char *) addr;

	return bp;
}

static sc_ptrace_break_t *
sc_ptrace_find_breakpoint(sc_ptrace_t *ctx, unsigned long rip)
{
	unsigned int i;

	rip -= 1;
	for (i = 0; i < ctx->num_breakpoints; ++i) {
		sc_ptrace_break_t *bp = &ctx->breakpoints[i];

		if (bp->active && bp->address == rip)
			return bp;
	}

	return NULL;
}

static void
sc_ptrace_single_step(sc_ptrace_t *ctx, pid_t pid)
{
	if (ptrace(PTRACE_SINGLESTEP, pid, 0, 0) < 0)
		perror("ptrace(PTRACE_SINGLESTEP)");
	if (sc_ptrace_debug(ctx))
		printf("single stepping\n");
}

static void
sc_ptrace_step_over(sc_ptrace_t *ctx, sc_ptrace_break_t *bp, pid_t pid, struct user_regs_struct *regs)
{
	sc_ptrace_disable_breakpoint(ctx, bp);

	if (bp->permanent)
		ctx->step_over_break = bp;

	regs->rip = bp->address;
	ptrace(PTRACE_SETREGS, pid, NULL, regs);

	sc_ptrace_single_step(ctx, pid);
}

static int
sc_ptrace_check_call(sc_ptrace_t *ctx, pid_t pid, sc_ptrace_function_t *func, unsigned long rsp)
{
	unsigned long stack_word;

	if (rsp >= func->stack_on_entry)
		return 0;

	stack_word = ptrace(PTRACE_PEEKDATA, pid, rsp, 0);

	if (sc_ptrace_debug(ctx))
		printf("top of stack %lx\n", stack_word);

	if (func->start_addr <= stack_word && stack_word < func->end_addr) {
		sc_ptrace_enable_temporary_breakpoint(ctx, func->return_breakpoint, stack_word);
		return 1;
	}

	return 0;
}

int
sc_ptrace_start(void)
{
	sc_ptrace_t *ctx;
	pid_t pid;
	int status;
	sc_ptrace_function_t *func = NULL;

	if (!(ctx = sc_ptrace_get_context()))
		return -1;

	if (ctx->num_functions == 0) {
		printf("Nothing to trace\n");
		return 0;
	}

	if (ctx->child_pid != 0)
		return 0;

	ctx->child_pid = fork();
	if (ctx->child_pid < 0)
		return -1;

	if (ctx->child_pid == 0) {
		ptrace(PTRACE_TRACEME, 0, 0, 0);
		raise(SIGSTOP);
		return 0;
	}

#if 0
	if (ptrace(PTRACE_ATTACH, ctx->child_pid, 0, 0) < 0)
		perror("ptrace(PTRACE_ATTACH)");
#endif

	ctx->debug_enabled = 0;

	while ((pid = wait(&status)) != 0) {
		struct user_regs_struct regs;
		sc_ptrace_break_t *bp;

		if (pid < 0) {
			if (errno == ECHILD)
				break;
			perror("wait");
			continue;
		}

		if (WIFEXITED(status)) {
			if (sc_ptrace_debug(ctx))
				printf("pid exited status=%d\n", WEXITSTATUS(status));
			continue;
		}
		if (WIFSIGNALED(status)) {
			if (sc_ptrace_debug(ctx))
				printf("pid received signal %d\n", WTERMSIG(status));
			continue;
		}

		if (!WIFSTOPPED(status)) {
			if (sc_ptrace_debug(ctx))
				printf("pid %d status 0x%x\n", pid, status);
			continue;
		}

		if (WSTOPSIG(status) == SIGSEGV) {
			if (sc_ptrace_debug(ctx))
				printf("Something went wrong; tracee received SIGSEGV. Detaching.\n");
			sc_ptrace_detach(ctx);
			break;
		}

		ptrace(PTRACE_GETREGS, pid, NULL, &regs);

		if (sc_ptrace_debug(ctx))
			printf("pid stopped by signal %d; rip 0x%llx; rsp 0x%llx\n",
					WSTOPSIG(status), regs.rip, regs.rsp);

		bp = sc_ptrace_find_breakpoint(ctx, regs.rip);

		if (bp == NULL) {
			sc_context_add_sample(sc_context, (caddr_t) regs.rip);
		}

		if (bp == NULL && func == NULL)
			goto release;

		if (bp != NULL) {
			bp->num_hits++;

			if (sc_ptrace_debug(ctx))
				printf("bp %lx hit %u times\n", bp->address, bp->num_hits);

			/* record a sample for the instruction under the breakpoint */
			sc_context_add_sample(sc_context, (caddr_t) bp->address);

			sc_ptrace_step_over(ctx, bp, pid, &regs);
			func = bp->owner;
			if (bp == func->entry_breakpoint)
				func->stack_on_entry = regs.rsp;
			continue;
		}

		if (func != NULL && func->start_addr <= regs.rip && regs.rip <= func->end_addr) {
			sc_ptrace_single_step(ctx, pid);
			continue;
		}

		/* Catch function calls inside a traced function.
		 * This assumes the stack grows down, and the top-most word on the stack will be
		 * the return address. */
		sc_ptrace_check_call(ctx, pid, func, regs.rsp);

release:
		sc_ptrace_enable_breakpoints(ctx);

		if (ptrace(PTRACE_CONT, pid, 0, 0) < 0)
			perror("ptrace(PTRACE_CONT)");

		if (sc_ptrace_debug(ctx))
			printf("should be running again...\n");
	}

	return 0;
}

int
sc_sampling_ptrace_function(caddr_t start_addr, caddr_t end_addr)
{
	sc_ptrace_t *ctx;
	sc_ptrace_function_t *f;

	if (!(ctx = sc_ptrace_get_context()))
		return -1;

	if ((ctx->num_functions % 8) == 0)
		ctx->functions = realloc(ctx->functions, (ctx->num_functions + 8) * sizeof(ctx->functions[0]));

	f = &ctx->functions[ctx->num_functions++];
	memset(f, 0, sizeof(*f));

	f->start_addr = (unsigned long) start_addr;
	f->end_addr = (unsigned long) end_addr;

	f->entry_breakpoint = sc_ptrace_create_breakpoint(ctx, f->start_addr, 1, f);
	f->return_breakpoint = sc_ptrace_create_breakpoint(ctx, 0, 0, f);

	return 0;
}
