/* proc.c -- process control system calls ($Revision: 1.2 $) */

#include "es.h"

#define	EWINTERRUPTIBLE	1
#define	EWNOHANG	2
#define	EWCONTINUE	4

Boolean hasforked = FALSE;
int *forkpgid = NULL;

typedef enum { LIVE, STOPPED, DEAD } procstate;

typedef struct Proc Proc;
struct Proc {
	int pid, pgid, status;
	procstate state;
	Proc *next, *prev;
};

static Proc *proclist = NULL;

static int ttyfd = -1;
static pid_t espgid;
#if JOB_PROTECT
static pid_t tcpgid0;
#endif

/* mkproc -- create a Proc structure */
extern Proc *mkproc(int pid) {
	Proc *proc = ealloc(sizeof (Proc));
	proc->next = proclist;
	proc->pid = pid;
	proc->pgid = 0;
	proc->state = LIVE;
	proc->prev = NULL;
	return proc;
}

/* efork -- fork (if necessary) and clean up as appropriate */
extern int efork(Boolean parent) {
	int pid;
	Boolean childpgrp = FALSE;
	if (!parent)
		goto cleanup;
	pid = fork();
	switch (pid) {
	default: {	/* parent */
		Proc *proc = mkproc(pid);
		if (forkpgid != NULL) {
			if (*forkpgid == 0)
				*forkpgid = pid;
			proc->pgid = *forkpgid;
			if (setpgid(pid, proc->pgid) < 0)
				fail("es:efork", "setpgid: %s", esstrerror(errno));
		}
		if (proclist != NULL)
			proclist->prev = proc;
		proclist = proc;
		return pid;
	}
	case 0:		/* child */
		if (forkpgid != NULL) {
			if (setpgid(0, (*forkpgid != 0) ? *forkpgid : getpid()) < 0) {
				eprint("child setpgid: %s", esstrerror(errno));
				esexit(1);
			}
			childpgrp = TRUE;
			forkpgid = NULL;
		}
		while (proclist != NULL) {
			Proc *p = proclist;
			proclist = proclist->next;
			efree(p);
		}
		hasforked = TRUE;
#if JOB_PROTECT
		tcpgid0 = 0;
#endif
		break;
	case -1:
		fail("es:efork", "fork: %s", esstrerror(errno));
	}

cleanup:
	closefds();
	setsigdefaults(childpgrp);
	newchildcatcher();
	return 0;
}

extern pid_t spgrp(pid_t pgid) {
	pid_t old = getpgrp();
	setpgid(0, pgid);
	espgid = pgid;
	return old;
}

static int tcspgrp(pid_t pgid) {
	int e = 0;
	Sigeffect tstp, ttin, ttou;
	if (ttyfd < 0)
		return ENOTTY;
	tstp = esignal(SIGTSTP, sig_ignore);
	ttin = esignal(SIGTTIN, sig_ignore);
	ttou = esignal(SIGTTOU, sig_ignore);
	if (tcsetpgrp(ttyfd, pgid) != 0)
		e = errno;
	esignal(SIGTSTP, tstp);
	esignal(SIGTTIN, ttin);
	esignal(SIGTTOU, ttou);
	return e;
}

extern int tctakepgrp(void) {
	pid_t tcpgid = 0;
	if (ttyfd < 0)
		return ENOTTY;
	tcpgid = tcgetpgrp(ttyfd);
	if (espgid == 0 || tcpgid == espgid)
		return 0;
	return tcspgrp(espgid);
}

extern void initpgrp(void) {
	espgid = getpgrp();
	ttyfd = opentty();
#if JOB_PROTECT
	if (ttyfd >= 0)
		tcpgid0 = tcgetpgrp(ttyfd);
#endif
}

#if JOB_PROTECT
extern void tcreturnpgrp(void) {
	if (tcpgid0 != 0 && ttyfd >= 0 && tcpgid0 != tcgetpgrp(ttyfd))
		tcspgrp(tcpgid0);
}

extern Noreturn esexit(int code) {
	tcreturnpgrp();
	exit(code);
}
#endif

/* dowait -- a waitpid wrapper that interfaces with signals */
static int dowait(int pid, int opts, int *statusp) {
	int n;
	int waitopts = /* WCONTINUED | */ WUNTRACED;
	if (opts & EWNOHANG)
		waitopts |= WNOHANG;
	interrupted = FALSE;
	if (!setjmp(slowlabel)) {
		slow = TRUE;
		n = interrupted ? -2 :
			waitpid(pid, statusp, waitopts);
	} else
		n = -2;
	slow = FALSE;
	if (n == -2) {
		errno = EINTR;
		n = -1;
	}
	return n;
}

/* reap -- mark a process's state, pull it out of proclist, and return status */
static List *reap(int *pid, int status, char **state) {
	int deadpid, deadpgid;
	Proc *proc, *p;
	procstate pgidstate = DEAD;

	for (proc = proclist; proc != NULL; proc = proc->next)
		if (proc->pid == *pid)
			break;
	assert(proc != NULL);

	deadpid = proc->pid;
	deadpgid = proc->pgid;

	/* if (WIFCONTINUED(status)) {
		proc->state = LIVE;
		return NULL;
	} */
	if (WIFSTOPPED(status))
		proc->state = STOPPED;
	else
		proc->state = DEAD;
	proc->status = status;

	*state = (WIFSTOPPED(status) ? "stopped"
			: WIFSIGNALED(status) ? "signaled"
			: "exited");

	for (p = proclist; p != NULL; p = p->next) {
		if ((p->pgid < 1 && p->pid != deadpid) || (p->pgid != deadpgid))
			continue;
		if (p->state == LIVE)
			return NULL;
		else if (p->state == STOPPED)
			pgidstate = STOPPED;
	}

	Ref(List *, statuslist, NULL);
	p = proclist;
	while (p != NULL) {
		Proc *freeit = NULL;
		if ((p->pgid < 1 && p->pid != deadpid) || (p->pgid != deadpgid)) {
			p = p->next;
			continue;
		}
		/* build the status list backwards, because proclist is backwards
		 * and two backwards makes a forwards */
		gcdisable();
		statuslist = mklist(mkstr(mkstatus(p->status)), statuslist);
		gcenable();
		if (pgidstate == DEAD) {
			freeit = p;
			if (p->next != NULL)
				p->next->prev = p->prev;
			if (p->prev != NULL)
				p->prev->next = p->next;
			else
				proclist = p->next;
		}
		p = p->next;
		if (freeit != NULL)
			efree(freeit);
	}
	*pid = (length(statuslist) > 1 ? -deadpgid : deadpid);
	RefReturn(statuslist);
}

static int pidtopgid(int pid) {
	Proc *proc;
	for (proc = proclist; proc != NULL; proc = proc->next)
		if (pid == proc->pid) {
			if (proc->pgid > 0)
				return -proc->pgid;
			else
				return pid;
		}
	/* return the pid, let waitpid() barf on it */
	return pid;
}

/* TODO: returning a gcalloc'd List * here is the source of a lot of issues.
 * change to a list of Proc *s or a custom ealloc'd status struct! */
extern List *ewait(int pa, int opts) {
	int status, deadpid;
	volatile Boolean returnnull = FALSE;
	char *volatile state = NULL;
	volatile int pidarg = (pa == 0) ? -1 : pidtopgid(pa);
	Ref(List *, statuslist, NULL);

	if (pidarg < -1)
		tcspgrp(-pidarg);

	/* TODO: make sure these exceptions are formatted ok */
	/* TODO: returnnull is a gross hack. fix that */
	/* TODO: 'state' is also a gross hack. fix that */

	ExceptionHandler

		if (opts & EWCONTINUE) {
			if (pidarg == -1)
				fail("es:ewait", "wait -c requires a pid or pgid argument");
			else if (kill(pidarg, SIGCONT) < 0)
				fail("es:ewait", "continue: %s", esstrerror(errno));
			/* TODO: set proc states here to LIVE? don't have WCONTINUED in POSIX.1-2001 */
		}

		do {
			while ((deadpid = dowait(pidarg, (opts & EWNOHANG), &status)) == -1) {
				if (errno == ECHILD) {
					if ((opts & EWNOHANG) && pidarg == -1) {
						returnnull = TRUE;
						deadpid = 0;
						break;
					}
					if (pidarg != -1)
						fail("es:ewait", "wait: %d is not a child of this shell", pidarg);
				}
				if (errno != EINTR)
					fail("es:ewait", "wait: %s", esstrerror(errno));
				if (opts & EWINTERRUPTIBLE)
					SIGCHK();
			}
			if (deadpid == 0) {	/* dowait(EWNOHANG) returned nothing */
				returnnull = TRUE;
				break;
			}
			if (!returnnull)
				statuslist = reap(&deadpid, status, (char **) &state);
		} while (statuslist == NULL);

	CatchException (e)

#if JOB_PROTECT
		tctakepgrp();
#endif
		throw(e);

	EndExceptionHandler

#if JOB_PROTECT
	tctakepgrp();
#endif
	if (returnnull) {
		RefPop(statuslist);
		return NULL;
	}
	/* has to be down here because we need to have the terminal back before printing anything */
	/* should this be called even when returning NULL? */
	printstatus(deadpid, state, statuslist);
	RefReturn(statuslist);
}

#include "prim.h"

PRIM(apids) {
	size_t i, n = 0, nprocs = 0;
	int *pids, pidmatch = 0;
	Proc *p;
	if (list != NULL) {
		char *end;
		if (list->next != NULL)
			fail("$&apids", "usage: apids [pid|-pgid]");
		pidmatch = strtol(getstr(list->term), &end, 0);
		if (*end != '\0')
			fail("$&apids", "bad pid %E -- usage: apids [pid|-pgid]", list->term);
	}
	for (p = proclist; p != NULL; p = p->next)
		nprocs++;
	pids = ealloc(sizeof(int) * nprocs);
	Ref(List *, lp, NULL);
	for (p = proclist; p != NULL; p = p->next) {
		Boolean donepid = FALSE;
		int pid = (pidmatch == 0 && p->pgid > 0) ? -p->pgid : p->pid;
		Term *t;
		if ((pidmatch < 0 && pidmatch != -p->pgid) || (pidmatch > 0 && pidmatch != p->pid))
			continue;
		for (i = 0; i < n; i++)
			if (pid == pids[i]) {
				donepid = TRUE;
				break;
			}
		if (donepid)
			continue;
		pids[n++] = pid;
		t = mkstr(str("%d", pid));
		lp = mklist(t, lp);
	}
	efree(pids);
	RefReturn(lp);
}

PRIM(wait) {
	int pid = -1, opts = EWINTERRUPTIBLE;
	char *usage = "usage: wait [-cn] [pid|-pgid]";
	Ref(List *, lp, list);
	while (lp != NULL) {
		char *arg = getstr(lp->term);
		if (arg[0] != '-' || ('0' <= arg[1] && arg[1] <= '9'))
			break;
		if (arg[1] == '\0')
			fail("$&wait", usage);
		for (arg = arg+1; *arg != '\0'; arg++) {
			switch (*arg) {
			case 'c': opts |= EWCONTINUE; break;
			case 'n': opts |= EWNOHANG; break;
			default:
				fail("$&wait", "illegal option: -%c -- %s", *arg, usage);
			}
		}
		lp = lp->next;
	}

	if (lp != NULL) {
		Boolean neg = FALSE;
		char *s = getstr(lp->term);
		if (*s == '-') {
			neg = TRUE;
			s += 1;
		}
		pid = atoi(s);
		if (pid <= 0) {
			fail("$&wait", "wait: %s: bad pid -- %s", s, usage);
			NOTREACHED;
		}
		if (neg)
			pid = -pid;
		lp = lp->next;
	}
	if (lp != NULL)
		fail("$&wait", usage);
	RefEnd(lp);
	return ewait(pid, opts);
}

extern Dict *initprims_proc(Dict *primdict) {
	X(apids);
	X(wait);
	return primdict;
}
