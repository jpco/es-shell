/* proc.c -- process control system calls ($Revision: 1.2 $) */

#if HAVE_GETRUSAGE
#include <sys/time.h>
#include <sys/resource.h>
#endif

typedef struct Proc Proc;
typedef struct Job Job;

struct Proc {
	int pid;
	int status;
	Boolean alive, stopped;
	Proc *next, *prev;
	Job *job;
#if HAVE_GETRUSAGE
	struct rusage rusage;
#endif
};

struct Job {
	int pgid;
	Boolean alive, stopped;
	Proc *proclist;
	Job *next, *prev;
};

/* processes which are not grouped under jobs and are mostly in the shell pgrp. */
extern Proc *proclist;

/* processes grouped under jobs, running in the jobs' pgrps. */
extern Job *joblist;

/* `forkjob` informs efork() to put the new Procs it creates into a Job. */
typedef struct jobcontext jobcontext;
struct jobcontext {
	Job *job;
};
extern jobcontext *forkjob;

/* Is job control active?  FIXME: Is this actually necessary? */
extern Boolean jobcontrol;

/* Are we in a forked child process? */
extern Boolean hasforked;

extern int pidtopgid(int);
