/* proc.c -- process control system calls ($Revision: 1.2 $) */

#include "es.h"
#include "proc.h"

/* TODO: the rusage code for the time builtin really needs to be cleaned up */

#if HAVE_GETRUSAGE
#include <sys/time.h>
#include <sys/resource.h>
#endif

Boolean hasforked = FALSE;
Proc *proclist = NULL;
Job *joblist = NULL;

/* mkproc -- create a Proc structure */
static Proc *mkproc(int pid, Job *job) {
	Proc *proc;
	Proc *list = (job == NULL ? proclist : job->proclist);
	for (proc = list; proc != NULL; proc = proc->next)
		if (proc->pid == pid) {		/* are we recycling pids? */
			assert(!proc->alive);	/* if false, violates unix semantics */
			break;
		}
	if (proc == NULL) {
		proc = ealloc(sizeof (Proc));
		proc->next = list;
	}
	proc->pid = pid;
	proc->alive = TRUE;
	proc->prev = NULL;
	proc->job = job;
	return proc;
}

/* mkjob -- create a Job structure */
static Job *mkjob(int pgid) {
	Job *job;
	for (job = joblist; job != NULL; job = job->next)
		if (job->pgid == pgid) {
			assert(!job->alive);
			break;
		}
	if (job == NULL) {
		job = ealloc(sizeof (Job));
		job->next = joblist;
	}
	job->pgid = pgid;
	job->alive = TRUE;
	job->proclist = NULL;
	job->prev = NULL;
	return job;
}

/* efork -- fork (if necessary) and clean up as appropriate */
extern int efork(Boolean parent) {
	if (parent) {
		int pid;
		Job *job = (forkjob == NULL ? NULL : forkjob->job);

		pid = fork();
		switch (pid) {
		default: {	/* parent */
			Proc *proc;
			if (job == NULL && forkjob != NULL) {
				forkjob->job = mkjob(pid);
				job = forkjob->job;
				if (joblist != NULL)
					joblist->prev = job;
				joblist = job;
			}
			proc = mkproc(pid, job);

			if (job != NULL) {
				if (setpgrp(pid, job->pgid) < 0)
					fail("es:efork", "setpgrp: %s", esstrerror(errno));
				if (job->proclist != NULL)
					job->proclist->prev = proc;
				job->proclist = proc;
			} else {
				if (proclist != NULL)
					proclist->prev = proc;
				proclist = proc;
			}
			return pid;
		}
		case 0:		/* child */
			if (forkjob != NULL)
				if (setpgrp(0, (job != NULL ? job->pgid : getpid())) < 0) {
					eprint("child setpgrp: %s", esstrerror(errno));
					exit(1);
				}
			proclist = NULL;
			joblist = NULL;
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
	Job *job;
	for (proc = proclist; proc != NULL; proc = proc->next)
		if (proc->pid == pid) {
			assert(proc->alive);
			proc->alive = FALSE;
			proc->status = status;
			return proc;
		}
	for (job = joblist; job != NULL; job = job->next)
		for (proc = job->proclist; proc != NULL; proc = proc->next)
			if (proc->pid == pid) {
				assert(proc->alive);
				proc->alive = FALSE;
				proc->status = status;
				return proc;
			}
	return NULL;
}

/* Scan a Job and return true if all its Procs are dead. */
static Boolean scanjob(Job *job) {
	Proc *proc;
	Boolean alive = FALSE;
	for (proc = job->proclist; proc != NULL; proc = proc->next) {
		if (proc->alive)
			alive = TRUE;
	}
	job->alive = alive;
	return !alive;
}

/* Free a Proc which is in proclist. */
static int freeproc(Proc *proc) {
	int pid;
	assert(proc != NULL);
	pid = proc->pid;
	if (proc->next != NULL)
		proc->next->prev = proc->prev;
	if (proc->prev != NULL)
		proc->prev->next = proc->next;
	else
		proclist = proc->next;

	efree(proc);
	return pid;
}

/* Free a Job which is in joblist, and the Procs it contains. */
static int freejob(Job *job) {
	int pgid;
	Proc *proc;
	assert(job != NULL);
	pgid = job->pgid;

	for (proc = job->proclist; proc != NULL; proc = proc->next)
		efree(proc);

	if (job->next != NULL)
		job->next->prev = job->prev;

	if (job->prev != NULL)
		job->prev->next = job->next;
	else
		joblist = job->next;

	efree(job);

	return pgid;
}

#if !HAVE_WAITPID

/* Limited imitation of waitpid().  Can't power job control, and marks some
 * background procs as dead earlier than the real one does, but that's ok.
 * Because it wait()s for wrong children, it needs to manage some rusages
 * itself. */
static int fakewaitpid(int pid, int *statusp, int opts) {
	int deadpid, status;
	Proc *p;
#if HAVE_GETRUSAGE
	struct rusage rusage;
#endif

	if (pid < -1 || (opts != 0 && opts != WUNTRACED)) {
		errno = EINVAL;
		return -1;
	}

	if (pid < 1) {
		for (p = proclist; p != NULL; p = p->next)
			if (!p->alive) {
				*statusp = p->status;
				/* hack!!! */
				p->alive = TRUE;
				return p->pid;
			}
		deadpid = wait(&status);
	}

	while ((deadpid = wait(&status)) != pid) {
		if (deadpid == -1)
			return deadpid;
	}

	for (p = proclist; p != NULL; p = p->next)
		if (p->pid == deadpid) {
			break;
		}
	assert(p != NULL);

	p->alive = FALSE;
	p->status = status;
#if HAVE_GETRUSAGE
	getrusage(RUSAGE_CHILDREN, &rusage);
	p->rusage = rusage;
#endif
	*statusp = status;
	return deadpid;
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

/* If the pid is in a job pgid, return the job's pgid.  Otherwise return 0. */
static int pidtopgid(int pid) {
	Job *job;
	Proc *proc;
	for (job = joblist; job != NULL; job = job->next)
		for (proc = job->proclist; proc != NULL; proc = proc->next)
			if (proc->pid == pid)
				return job->pgid;
	return 0;
}

/* ewait -- wait for a specific process to die, or any process if pid == 0 */
extern List *ewait(int pid, Boolean interruptible, void *rusage) {
	Proc *proc;
	int pgid, status, deadpid = 0;

	if ((pgid = pidtopgid(pid)) > 0)
		pid = -pgid;

	Ref(List *, lp, NULL);
	while (true) {
		Boolean seen_eintr = FALSE;
		/* Hack pid 0 to -1: background procs may have been setpgid/setsid elsewhere */
		while ((deadpid = dowaitpid(pid == 0 ? -1 : pid, &status, WUNTRACED)) == -1)
			if (errno == EINTR) {
				if (interruptible)
					SIGCHK();
				seen_eintr = TRUE;
			} else if (errno == ECHILD && seen_eintr)
				/* TODO: not clear on why this is necessary
				 * (sometimes child procs disappear after SIGINT) */
				break;
			else {
				fail("es:ewait", "wait: %s", esstrerror(errno));
			}

		proc = reap(deadpid, status);

		if (proc->job == NULL) {
#if HAVE_GETRUSAGE
			if (rusage != NULL)
#if HAVE_WAITPID
				getrusage(RUSAGE_CHILDREN, rusage);
#else
				memcpy(rusage, &proc->rusage, sizeof (struct rusage));
#endif
#else
			assert(rusage == NULL);
#endif
			lp = mklist(mkstr(mkstatus(proc->status)), NULL);
			freeproc(proc);
			break;
		}

		if (scanjob(proc->job)) {
			Proc *p;
#if HAVE_GETRUSAGE
			/* FIXME: do we need to run a getrusage at the top to "clear" the getrusage state? */
			if (rusage != NULL)
				getrusage(RUSAGE_CHILDREN, rusage);
#else
			assert(rusage == NULL);
#endif
			for (p = proc->job->proclist; p != NULL; p = p->next)
				lp = mklist(mkstr(mkstatus(p->status)), lp);
			freejob(proc->job);
			break;
		}
	}
	RefReturn(lp);
}

#include "prim.h"

PRIM(apids) {
	Proc *p;
	Job *j;
	Ref(List *, lp, NULL);
	for (p = proclist; p != NULL; p = p->next)
		if (p->alive) {
			Term *t = mkstr(str("%d", p->pid));
			lp = mklist(t, lp);
		}
	for (j = joblist; j != NULL; j = j->next)
		if (j->alive) {
			Term *t = mkstr(str("%d", -j->pgid));
			lp = mklist(t, lp);
		}
	/* TODO: sort by absolute value */
	RefReturn(lp);
}

PRIM(wait) {
	int pid;
	if (list == NULL)
		pid = 0;
	else if (list->next == NULL) {
		pid = atoi(getstr(list->term));
	} else {
		fail("$&wait", "usage: wait [pid]");
		NOTREACHED;
	}
	Ref(List *, status, ewait(pid, TRUE, NULL));
	printstatus(pid, status);
	RefReturn(status);
}

extern Dict *initprims_proc(Dict *primdict) {
	X(apids);
	X(wait);
	return primdict;
}
