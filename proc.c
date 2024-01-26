/* proc.c -- process control system calls ($Revision: 1.2 $) */

#include "es.h"

/* TODO: the rusage code for the time builtin really needs to be cleaned up */

#if HAVE_GETRUSAGE
#include <sys/time.h>
#include <sys/resource.h>
#endif

Boolean hasforked = FALSE;

typedef struct Proc Proc;
struct Proc {
	int pid;
	int status;
	Boolean alive, background;
	Proc *next, *prev;
#if HAVE_GETRUSAGE
	struct rusage rusage;
#endif
};

static Proc *proclist = NULL;

/* mkproc -- create a Proc structure */
extern Proc *mkproc(int pid, Boolean background) {
	Proc *proc;
	for (proc = proclist; proc != NULL; proc = proc->next)
		if (proc->pid == pid) {		/* are we recycling pids? */
			assert(!proc->alive);	/* if false, violates unix semantics */
			break;
		}
	if (proc == NULL) {
		proc = ealloc(sizeof (Proc));
		proc->next = proclist;
	}
	proc->pid = pid;
	proc->alive = TRUE;
	proc->background = background;
	proc->prev = NULL;
	return proc;
}

/* efork -- fork (if necessary) and clean up as appropriate */
extern int efork(Boolean parent, Boolean background) {
	if (parent) {
		int pid = fork();
		switch (pid) {
		default: {	/* parent */
			Proc *proc = mkproc(pid, background);
			if (proclist != NULL)
				proclist->prev = proc;
			proclist = proc;
			return pid;
		}
		case 0:		/* child */
			proclist = NULL;
			hasforked = TRUE;
			break;
		case -1:
			fail("es:efork", "fork: %s", esstrerror(errno));
		}
	}
	closefds();
	setsigdefaults();
	newchildcatcher();
	return 0;
}

/* reap -- mark a process as dead and attach its exit status */
static Proc *reap(int pid, int status) {
	Proc *proc;
#if HAVE_GETRUSAGE
	struct rusage rusage;
	getrusage(RUSAGE_CHILDREN, &rusage);
#endif
	for (proc = proclist; proc != NULL; proc = proc->next)
		if (proc->pid == pid) {
			assert(proc->alive);
			proc->alive = FALSE;
			proc->status = status;
#if HAVE_GETRUSAGE
			proc->rusage = rusage;
#endif
			return proc;
		}
	return NULL;
}

static void unlist(Proc *proc, Proc **list) {
	assert(proc != NULL && list != NULL);
	if (proc->next != NULL)
		proc->next->prev = proc->prev;
	if (proc->prev != NULL)
		proc->prev->next = proc->next;
	else
		*list = proc->next;
}

#if !HAVE_WAITPID

/* Limited imitation of waitpid().  Can't power job control, and marks some
 * background procs as dead earlier than the real one does, but that's ok. */
static int fakewaitpid(int pid, int *statusp, int opts) {
	int deadpid, status;
	Proc *p;

	if (pid < -1 || (opts != 0 && opts != WUNTRACED)) {
		errno = EINVAL;
		return -1;
	}

	/* return an already-dead proc or something new */
	if (pid < 1) {
		for (p = proclist; p != NULL; p = p->next)
			if (!p->alive) {
				*statusp = p->status;
				/* hack!!! */
				p->alive = TRUE;
				return p->pid;
			}
		return wait(statusp);
	}

	while ((deadpid = wait(&status)) != pid) {
		if (deadpid == -1)
			return deadpid;
		for (p = proclist; p != NULL; p = p->next)
			if (p->pid == deadpid) {
				p->alive = FALSE;
				p->status = status;
				break;
			}
		assert(p != NULL);
	}

	*statusp = status;
	return pid;
}

#endif

/* dowaitpid -- a waitpid wrapper that interfaces with signals */
static int dowaitpid(int pid, int *statusp, int opts) {
	int n;
	interrupted = FALSE;
	if (!setjmp(slowlabel)) {
		slow = TRUE;
		n = interrupted ? -2 :
#if HAVE_WAITPID
			waitpid(pid, (void *) statusp, opts);
#else
			fakewaitpid(pid, (void *) statusp, opts);
#endif
	} else
		n = -2;
	slow = FALSE;
	if (n == -2) {
		errno = EINTR;
		n = -1;
	}
	return n;
}

/* ewait -- wait for a specific process to die, or any process if pid == 0 */
extern int ewait(int pid, Boolean interruptible, void *rusage) {
	Proc *proc;
	int deadpid, status;
	Boolean seen_eintr = FALSE;
	/* Hack pid to -1: background procs may have been setpgid/setsid elsewhere */
	while ((deadpid = dowaitpid(pid == 0 ? -1 : pid, &status, WUNTRACED)) == -1)
		if (errno == EINTR) {
			if (interruptible)
				SIGCHK();
			seen_eintr = TRUE;
		} else if (errno == ECHILD && seen_eintr)
			/* TODO: not clear on why this is necessary
			 * (sometimes child procs disappear after SIGINT) */
			break;
		else
			fail("es:ewait", "wait: %s", esstrerror(errno));

	proc = reap(deadpid, status);
	unlist(proc, &proclist);
	if (proc->background)
		printstatus(proc->pid, status);
	efree(proc);
	return status;
}

#include "prim.h"

PRIM(apids) {
	Proc *p;
	Ref(List *, lp, NULL);
	for (p = proclist; p != NULL; p = p->next)
		if (p->background && p->alive) {
			Term *t = mkstr(str("%d", p->pid));
			lp = mklist(t, lp);
		}
	/* TODO: sort the return value, but by number? */
	RefReturn(lp);
}

PRIM(wait) {
	int pid;
	if (list == NULL)
		pid = 0;
	else if (list->next == NULL) {
		pid = atoi(getstr(list->term));
		if (pid <= 0) {
			fail("$&wait", "wait: %d: bad pid", pid);
			NOTREACHED;
		}
	} else {
		fail("$&wait", "usage: wait [pid]");
		NOTREACHED;
	}
	return mklist(mkstr(mkstatus(ewait(pid, TRUE, NULL))), NULL);
}

extern Dict *initprims_proc(Dict *primdict) {
	X(apids);
	X(wait);
	return primdict;
}
