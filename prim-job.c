/* prim-job.c -- job control primitives */

#include "es.h"
#include "prim.h"
#include "proc.h"

Boolean jobcontrol = FALSE;
jobcontext *forkjob = NULL;

static int initpgid = -1;

/* Highly redundant with $&newpgrp :( */
static int enablejobcontrol(void) {
	int espgid, tcpgrp;
	Sigeffect tstp, ttin, ttou;

	while ((tcpgrp = tcgetpgrp(0)) != -1 && tcpgrp != (espgid = getpgrp()))
		kill(-espgid, SIGTTIN);
	if (tcpgrp == -1)
		return errno;

	espgid = getpid();
	if ((getpgrp() != espgid) && (setpgrp(0, espgid) < 0))
		return errno;

	if (tcgetpgrp(0) == espgid)
		return 0;

	tstp = esignal(SIGTSTP, sig_ignore);
	ttin = esignal(SIGTTIN, sig_ignore);
	ttou = esignal(SIGTTOU, sig_ignore);

	if (tcsetpgrp(0, espgid) < 0)
		return errno;

	esignal(SIGTSTP, tstp);
	esignal(SIGTTIN, ttin);
	esignal(SIGTTOU, ttou);

	return 0;
}

static int disablejobcontrol(void) {
	assert(initpgid >= 0);

	if (getpgrp() == initpgid)
		return 0;

	/* use stderr: on exit, stdin may have EOF'd */
	if (tcsetpgrp(2, initpgid) < 0)
		return errno;

	if (setpgrp(0, initpgid) < 0)
		return errno;

	return 0;
}

/* Enable or disable job control based on the truthiness of list.
 * Note that enabling job control may be impossible due to an error or other
 * factors, in which case the value returned here will instead be a stringified
 * message describing why job control is disabled.
 *
 * if you aren't toggling the binary value of jobcontrol, then this will let
 * you set whatever value you want. */

PRIM(setjobcontrol) {
	int err;
	Boolean enable = istrue(list);

	if (initpgid == -1)
		initpgid = getpgrp();

	if (jobcontrol == enable)
		return list;

	if (enable)
		if (isatty(0)) err = enablejobcontrol();
		else return mklist(mkstr("stdin is not a terminal"), NULL);
	else err = disablejobcontrol();

	if (err != 0) {
		/* unwinding enablement may fail too, but try it anyway. */
		if (enable) disablejobcontrol();
		return mklist(mkstr(esstrerror(err)), NULL);
	}

	jobcontrol = enable;
	return list;
}

/* make a new "job" (pgrp) for processes to be added into.  note that this
 * function doesn't actually _make_ the Job, but just creates a signal to
 * efork() that it will need to if it wants to make a new Proc */

PRIM(makejob) {
	jobcontext jc, *prev = NULL;

	if (jobcontrol) {
		/* start with no Job in the context.  this will be created when
		 * the first Proc is. */
		jc.job = NULL;

		/* we do a stack of jobcontexts, for hygiene reasons. */
		prev = forkjob;
		forkjob = &jc;
	} else {
		prev = forkjob;
		forkjob = NULL;
	}

	ExceptionHandler

		list = eval(list, NULL, evalflags);

	CatchException (e)

		forkjob = prev;
		throw(e);

	EndExceptionHandler

	/* we don't clean up jc.job in here -- if it isn't null, then it was
	 * also added to joblist, where it's now somebody else's problem. */
	forkjob = prev;
	return list;
}

/* FIXME: is it bad to send a SIGCONT to a job that's already running?
 * (i.e., we background a job then foreground it while it's running in bg) */
PRIM(fgjob) {
	int pid = 0, pgid;

	if (list == NULL || (list->next != NULL && list->next->next != NULL))
		fail("$&fgjob", "usage: fgjob pid");
	else
		pid = atoi(getstr(list->term));

	if ((pgid = pidtopgid(pid)) == 0)
		fail("$&fgjob", "%d is not in a child pgrp of this shell", pid);

	Ref(List *, status, ewait(pgid, (EWINTERRUPTIBLE|EWCONTINUE), NULL));
	status = reportstatus(status, binding);
	RefReturn(status);
}

PRIM(bgjob) {
	int pid = 0, pgid;

	if (list == NULL || (list->next != NULL && list->next->next != NULL))
		fail("$&bgjob", "usage: bgjob pid");
	else
		pid = atoi(getstr(list->term));

	if ((pgid = pidtopgid(pid)) == 0)
		fail("$&bgjob", "%d is not in a child pgrp of this shell", pid);

	if (kill(-pgid, SIGCONT) < 0)
		fail("$&bgjob", "continue: %s", esstrerror(errno));

	return true;
}

extern Dict *initprims_jobcontrol(Dict *primdict) {
	X(setjobcontrol);
	X(makejob);
	X(fgjob);
	X(bgjob);
	return primdict;
}
