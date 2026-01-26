/* prim-io.c -- input/output and redirection primitives ($Revision: 1.2 $) */

#include "es.h"
#include "gc.h"
#include "prim.h"

#include <limits.h>

static const char *caller;

static int getnumber(const char *s) {
	char *end;
	int result = strtol(s, &end, 0);

	if (*end != '\0' || result < 0)
		fail(caller, "bad number: %s", s);
	return result;
}

static Noreturn argcount(const char *s) {
	fail(caller, "argument count: usage: %s", s);
}

/*
 * $&handlefile opens a file and attaches it to a handle, which may be part of a
 * list and bound to a variable.  In order to actually use it, it must be
 * "applied" to an fd using the dup primitive.
 */

#include "term.h"

typedef struct Filehandle Filehandle;
struct Filehandle {
	int fd, refs;
	Filehandle *prev, *next;
};

Filehandle *handles = NULL;

/* forward declare redir_openfile */
static List *redir_openfile(int *, Boolean *, List *);

/* handlefile mode name */
PRIM(handlefile) {
	int fd;
	Filehandle *h;
	caller = "$&handlefile";
	if (length(list) != 2)
		argcount("%handlefile mode file");

	/* open file */
	list = redir_openfile(&fd, NULL, list);
	assert(list == NULL && fd != -1);

	/* create and link filehandle */
	h = ealloc(sizeof(Filehandle));
	h->fd = fd;
	h->refs = 1;
	h->prev = NULL;
	h->next = handles;
	if (handles != NULL)
		handles->prev = h;
	handles = h;

	/* return filehandle */
	Ref(List *, result, NULL);
	gcdisable();
	result = mklist(mkterm(NULL,
				mkclosure(gcmk(nHandle, (char *)h), NULL)),
			NULL);
	gcenable();
	RefReturn(result);
}


/*
 * The openfile, dup, and close primitives work via the redir() function.
 * This function takes three arguments: rop, list, and evalflags.
 *  - rop is a function which performs some kind of file-related action
 *  - list is a List containing inputs for rop(), including a command to run
 *  - evalflags is evalflags - critically, eval_inchild is used for deferring
 *
 * What redir() does is:
 *  - pop the first term from list and save it as an int in destfd
 *  - call rop() on the list, which sets srcfd and returns a new list
 *  - performs a close on destfd, OR calls dup2(srcfd, destfd) (with deferring)
 *  - eval()s the list
 *  - undefers
 *  - returns the result or throws the exception from the eval() call
 *
 * The rop() functions:
 *  - take a list containing information required for performing its file
 *    action, plus a command. this is fairly free-form, as different redirs have
 *    different inputs (e.g., file and mode for openfile but input fd for dup)
 *  - generate a realfd (srcfd) which can be "assigned" to the userfd (destfd)
 *  - return an eval()able command - probably the input list with some number of
 *    terms popped from the front
 */

static List *redir(List *(*rop)(int *fd, Boolean *ud, List *list), List *list, int evalflags) {
	int destfd, srcfd;
	Boolean ud = TRUE;
	volatile int inparent = (evalflags & eval_inchild) == 0;
	volatile int ticket = UNREGISTERED;

	assert(list != NULL);
	Ref(List *, lp, list);
	destfd = getnumber(getstr(lp->term));
	lp = (*rop)(&srcfd, &ud, lp->next);

	ticket = (srcfd == -1)
		   ? defer_close(inparent, destfd)
		   : defer_mvfd(inparent, srcfd, destfd);
	ExceptionHandler
		lp = eval(lp, NULL, evalflags);
		if (ud)
			undefer(ticket);
	CatchException (e)
		if (ud)
			undefer(ticket);
		throw(e);
	EndExceptionHandler

	RefReturn(lp);
}

#define	REDIR(name)	static List *CONCAT(redir_,name)(int *srcfdp, Boolean UNUSED *ud, List *list)

REDIR(openfile) {
	int i, fd;
	char *mode, *name;
	OpenKind kind;
	static const struct {
		const char *name;
		OpenKind kind;
	} modes[] = {
		{ "r",	oOpen },
		{ "w",	oCreate },
		{ "a",	oAppend },
		{ "r+",	oReadWrite },
		{ "w+",	oReadCreate },
		{ "a+",	oReadAppend },
		{ NULL, 0 }
	};

	/* assert(length(list) == 3); */
	Ref(List *, lp, list);

	mode = getstr(lp->term);
	lp = lp->next;
	for (i = 0;; i++) {
		if (modes[i].name == NULL)
			fail("$&openfile", "bad %%openfile mode: %s", mode);
		if (streq(mode, modes[i].name)) {
			kind = modes[i].kind;
			break;
		}
	}

	name = getstr(lp->term);
	lp = lp->next;
	fd = eopen(name, kind);
	if (fd == -1)
		fail("$&openfile", "%s: %s", name, esstrerror(errno));
	*srcfdp = fd;
	RefReturn(lp);
}

PRIM(openfile) {
	List *lp;
	caller = "$&openfile";
	if (length(list) != 4)
		argcount("%openfile mode fd file cmd");
	/* transpose the first two elements */
	lp = list->next;
	list->next = lp->next;
	lp->next = list;
	return redir(redir_openfile, lp, evalflags);
}

REDIR(dup) {
	int fd;
	assert(length(list) == 2);
	Ref(List *, lp, list);

	if (lp->term->closure != NULL
			&& lp->term->closure->tree->kind == nHandle) {
		Filehandle *h = (Filehandle *)(lp->term->closure->tree->u[0].s);
		fd = h->fd;
		*ud = FALSE;
	} else {
		fd = dup(fdmap(getnumber(getstr(lp->term))));
	}

	if (fd == -1)
		fail("$&dup", "dup: %s", esstrerror(errno));
	*srcfdp = fd;
	lp = lp->next;
	RefReturn(lp);
}

PRIM(dup) {
	caller = "$&dup";
	if (length(list) != 3)
		argcount("%dup newfd oldfd cmd");
	return redir(redir_dup, list, evalflags);
}

REDIR(close) {
	*srcfdp = -1;
	return list;
}

PRIM(close) {
	caller = "$&close";
	if (length(list) != 2)
		argcount("%close fd cmd");
	return redir(redir_close, list, evalflags);
}


/*
 * The other IO-related primitives (here, pipe, readfrom, writeto, backquote)
 * work via the pipefork() function.  This function performs a pipe(3) call on
 * its parameter p, and then performs a fork(3) and returns its output.
 *
 * The primary purpose of pipefork() is merely to abstract away some of the
 * noisy details around fd handling.
 *
 * CatchExceptionIf() exists to be used in this function: after forking, the
 * exception handler chain is discarded, so a normal CatchException would cause
 * an assertion failure.
 */

static int pipefork(int p[2], int *extra) {
	volatile int pid = 0;

	if (pipe(p) == -1)
		fail(caller, "pipe: %s", esstrerror(errno));

	registerfd(&p[0], FALSE);
	registerfd(&p[1], FALSE);
	if (extra != NULL)
		registerfd(extra, FALSE);

	ExceptionHandler
		pid = efork(TRUE, FALSE);
	CatchExceptionIf (pid != 0, e)
		unregisterfd(&p[0]);
		unregisterfd(&p[1]);
		if (extra != NULL)
			unregisterfd(extra);
		throw(e);
	EndExceptionHandler;

	unregisterfd(&p[0]);
	unregisterfd(&p[1]);
	if (extra != NULL)
		unregisterfd(extra);
	return pid;
}

PRIM(here) {
	int fd, doclen, p[2], status, ticket = UNREGISTERED;
	volatile int pid = -1;
	List *tail, **tailp;

	caller = "$&here";
	if (length(list) < 2)
		argcount("%here fd [word ...] cmd");

	fd = getnumber(getstr(list->term));
	Ref(List *, lp, list->next);
	for (tailp = &lp; (tail = *tailp)->next != NULL; tailp = &tail->next)
		;
	*tailp = NULL;

	Ref(List *, cmd, tail);
	Ref(char *, doc, (lp == tail) ? NULL : str("%L", lp, ""));
	doclen = strlen(doc);

#ifdef PIPE_BUF
	if (doclen <= PIPE_BUF) {
		if (pipe(p) == -1)
			fail("$&here", "pipe: %s", esstrerror(errno));
		ewrite(p[1], doc, doclen);
	} else
#endif
	if ((pid = pipefork(p, NULL)) == 0) {	/* child that writes to pipe */
		close(p[0]);
		ewrite(p[1], doc, doclen);
		esexit(0);
	}

	close(p[1]);
	ticket = defer_mvfd(TRUE, p[0], fd);

	ExceptionHandler
		lp = eval(cmd, NULL, evalflags);
	CatchException (e)
		undefer(ticket);
		close(p[0]);
		if (pid > 0)
			ewaitfor(pid);
		throw(e);
	EndExceptionHandler

	undefer(ticket);
	close(p[0]);
	if (pid > 0) {
		status = ewaitfor(pid);
		printstatus(0, status);
	}
	RefEnd2(doc, cmd);
	RefReturn(lp);
}

PRIM(pipe) {
	int n, infd, inpipe;
	static int *pids = NULL, pidmax = 0;

	caller = "$&pipe";
	n = length(list);
	if ((n % 3) != 1)
		fail("$&pipe", "usage: pipe cmd [ outfd infd cmd ] ...");
	n = (n + 2) / 3;
	if (n > pidmax) {
		pids = erealloc(pids, n * sizeof *pids);
		pidmax = n;
	}
	n = 0;

	infd = inpipe = -1;

	for (;; list = list->next) {
		int p[2], pid;

		pid = (list->next == NULL) ? efork(TRUE, FALSE) : pipefork(p, &inpipe);

		if (pid == 0) {		/* child */
			if (inpipe != -1) {
				assert(infd != -1);
				releasefd(infd);
				mvfd(inpipe, infd);
			}
			if (list->next != NULL) {
				int fd = getnumber(getstr(list->next->term));
				releasefd(fd);
				mvfd(p[1], fd);
				close(p[0]);
			}
			esexit(exitstatus(eval1(list->term, evalflags | eval_inchild)));
		}
		pids[n++] = pid;
		if (inpipe != -1)
			close(inpipe);
		if (list->next == NULL)
			break;
		list = list->next->next;
		infd = getnumber(getstr(list->term));
		inpipe = p[0];
		close(p[1]);
	}

	Ref(List *, result, NULL);
	do {
		Term *t;
		int status = ewaitfor(pids[--n]);
		printstatus(0, status);
		t = mkstr(mkstatus(status));
		result = mklist(t, result);
	} while (0 < n);
	if (evalflags & eval_inchild)
		esexit(exitstatus(result));
	RefReturn(result);
}

#if HAVE_DEV_FD
PRIM(readfrom) {
	int pid, p[2], status;
	Push push;

	caller = "$&readfrom";
	if (length(list) != 3)
		argcount("%readfrom var input cmd");
	Ref(List *, lp, list);
	Ref(char *, var, getstr(lp->term));
	lp = lp->next;
	Ref(Term *, input, lp->term);
	lp = lp->next;
	Ref(Term *, cmd, lp->term);

	if ((pid = pipefork(p, NULL)) == 0) {
		close(p[0]);
		mvfd(p[1], 1);
		esexit(exitstatus(eval1(input, evalflags &~ eval_inchild)));
	}

	close(p[1]);
	lp = mklist(mkstr(str(DEVFD_PATH, p[0])), NULL);
	varpush(&push, var, lp);

	ExceptionHandler
		lp = eval1(cmd, evalflags);
	CatchException (e)
		close(p[0]);
		ewaitfor(pid);
		throw(e);
	EndExceptionHandler

	close(p[0]);
	status = ewaitfor(pid);
	printstatus(0, status);
	varpop(&push);
	RefEnd3(cmd, input, var);
	RefReturn(lp);
}

PRIM(writeto) {
	int pid, p[2], status;
	Push push;

	caller = "$&writeto";
	if (length(list) != 3)
		argcount("%writeto var output cmd");
	Ref(List *, lp, list);
	Ref(char *, var, getstr(lp->term));
	lp = lp->next;
	Ref(Term *, output, lp->term);
	lp = lp->next;
	Ref(Term *, cmd, lp->term);

	if ((pid = pipefork(p, NULL)) == 0) {
		close(p[1]);
		mvfd(p[0], 0);
		esexit(exitstatus(eval1(output, evalflags &~ eval_inchild)));
	}

	close(p[0]);
	lp = mklist(mkstr(str(DEVFD_PATH, p[1])), NULL);
	varpush(&push, var, lp);

	ExceptionHandler
		lp = eval1(cmd, evalflags);
	CatchException (e)
		close(p[1]);
		ewaitfor(pid);
		throw(e);
	EndExceptionHandler

	close(p[1]);
	status = ewaitfor(pid);
	printstatus(0, status);
	varpop(&push);
	RefEnd3(cmd, output, var);
	RefReturn(lp);
}
#endif

#define	BUFSIZE	4096

static List *bqinput(const char *sep, int fd) {
	long n;
	char in[BUFSIZE];
	startsplit(sep, TRUE);

restart:
	/* avoid SIGCHK()ing in here so we don't abandon our child process */
	while ((n = read(fd, in, sizeof in)) > 0)
		splitstring(in, n, FALSE);
	if (n == -1) {
		if (errno == EINTR)
			goto restart;
		close(fd);
		fail("$&backquote", "backquote read: %s", esstrerror(errno));
	}
	return endsplit();
}

PRIM(backquote) {
	int pid, p[2], status;

	caller = "$&backquote";
	if (list == NULL)
		fail(caller, "usage: backquote separator command [args ...]");

	Ref(List *, lp, list);
	Ref(char *, sep, getstr(lp->term));
	lp = lp->next;

	if ((pid = pipefork(p, NULL)) == 0) {
		mvfd(p[1], 1);
		close(p[0]);
		esexit(exitstatus(eval(lp, NULL, evalflags | eval_inchild)));
	}

	close(p[1]);
	gcdisable();
	lp = bqinput(sep, p[0]);
	close(p[0]);
	status = ewaitfor(pid);
	printstatus(0, status);
	lp = mklist(mkstr(mkstatus(status)), lp);
	gcenable();
	list = lp;
	RefEnd2(sep, lp);
	SIGCHK();
	return list;
}


/*
 * These two primitives are just self-contained utilities.
 * $&newfd is a symptom of weak file handling and ideally should be removed.
 *
 * Arguably, $&read should probably be co-located with $&echo.
 * It is also worth considering why $&read and $&echo look so different from
 * one another: $&echo makes use of a lot of machinery also used elsewhere in
 * the shell, but doesn't the shell also read input outside of this one
 * primitive?
 */

PRIM(newfd) {
	if (list != NULL)
		fail("$&newfd", "usage: $&newfd");
	return mklist(mkstr(str("%d", newfd())), NULL);
}

/* read1 -- read one byte */
static int read1(int fd) {
	int nread;
	unsigned char buf;
	do {
		nread = read(fd, (char *) &buf, 1);
		SIGCHK();
	} while (nread == -1 && errno == EINTR);
	if (nread == -1)
		fail("$&read", "%s", esstrerror(errno));
	return nread == 0 ? EOF : buf;
}

PRIM(read) {
	int c;
	int fd = fdmap(0);

	static Buffer *buffer = NULL;
	if (buffer != NULL)
		freebuffer(buffer);
	buffer = openbuffer(0);

	while ((c = read1(fd)) != EOF && c != '\n')
		if (c == '\0')
			fail("$&read", "%%read: null character encountered");
		else
			buffer = bufputc(buffer, c);

	if (c == EOF && buffer->current == 0) {
		freebuffer(buffer);
		buffer = NULL;
		return NULL;
	} else {
		List *result = mklist(mkstr(sealcountedbuffer(buffer)), NULL);
		buffer = NULL;
		return result;
	}
}

extern Dict *initprims_io(Dict *primdict) {
	X(openfile);
	X(close);
	X(dup);
	X(handlefile);
	X(pipe);
	X(backquote);
	X(newfd);
	X(here);
#if HAVE_DEV_FD
	X(readfrom);
	X(writeto);
#endif
	X(read);
	return primdict;
}
