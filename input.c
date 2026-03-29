/* input.c -- read input from files or strings ($Revision: 1.2 $) */

#define	REQUIRE_STAT	1

#include "es.h"
#include "input.h"

/*
 * constants
 */

#define	BUFSIZE		((size_t) 4096)		/* buffer size to fill reads into */


/*
 * globals
 */

static Input *input = NULL;
Boolean resetterminal = FALSE;	/* TODO: localize to readline code */

#if HAVE_READLINE
#include <readline/readline.h>
#endif


/*
 * errors and warnings
 */

/* locate -- identify where an error came from */
static const char *locate(Input *in, const char *s) {
	return (in->runflags & run_interactive)
		? pstr("%s", s)
		: pstr("%s:%d: %s", in->name, in->lineno, s);
}

/* yyerror -- yacc error entry point */
extern void yyerror(Parser *p, const char *s) {
	if (p->error == NULL)	/* first error is generally the most informative */
		p->error = locate(p->input, s);
}

/* warn -- print a warning */
static void warn(Input *in, char *s) {
	eprint("warning: %s\n", locate(in, s));
}


/*
 * getting and ungetting characters
 */

static int fill(Input *in);
static void cleanup(Input *in);

/* get -- get a character, filter out nulls */
extern int get(Parser *p) {
	int c;
	Input *in = p->input;
	if (p->ungot > 0)
		return p->unget[--p->ungot];
	while ((c = (in->buf < in->bufend ? *in->buf++ : fill(in))) == '\0')
		warn(in, "null character ignored");
	if (c != EOF) {
		char buf = c;
		addhistbuffer(buf);
		if (p->input->runflags & run_echoinput)
			ewrite(2, &buf, 1);
	}
	return c;
}

/* unget -- push back one character */
extern void unget(Parser *p, int c) {
	assert(p->ungot < MAXUNGET);
	p->unget[p->ungot++] = c;
}



static int fill(Input *in) {
	long nread;
	assert(in->buf == in->bufend);

	if (in->fd < 0) {
		in->eof = TRUE;
		return EOF;
	}

#if HAVE_READLINE
	if (in->runflags & run_interactive && in->fd == 0) {
		char *rlinebuf = NULL;
		rl_instream = stdin;
		rl_outstream = stdout;
		do {
			rlinebuf = callreadline(in->prompt);
		} while (rlinebuf == NULL && errno == EINTR);
		if (rlinebuf == NULL)
			nread = 0;
		else {
			nread = strlen(rlinebuf) + 1;
			if (in->buflen < (unsigned int)nread) {
				while (in->buflen < (unsigned int)nread)
					in->buflen *= 2;
				in->bufbegin = erealloc(in->bufbegin, in->buflen);
			}
			memcpy(in->bufbegin, rlinebuf, nread - 1);
			in->bufbegin[nread - 1] = '\n';
			efree(rlinebuf);
		}
	} else
#endif
	do {
		nread = read(in->fd, (char *) in->bufbegin, in->buflen);
		SIGCHK();
	} while (nread == -1 && errno == EINTR);

	if (nread == -1)
		fail("$&parse", "%s: %s", in->name == NULL ? "es" : in->name, esstrerror(errno));
	if (nread == 0) {
		in->eof = TRUE;
		return EOF;
	}

	in->buf = in->bufbegin;
	in->bufend = &in->buf[nread];
	return *in->buf++;
}


/*
 * the input loop
 */

/* parse -- yyparse() wrapper */
extern Tree *parse(char *pr1, char *pr2) {
	int result;
	Parser p;
	void *oldpspace;

	if (input->eof) {
		input->eof = FALSE;
		throw(mklist(mkstr("eof"), NULL));
	}

	memzero(&p, sizeof (Parser));
	p.input = input;
	p.space = createpspace();
	oldpspace = setpspace(p.space);

	inityy(&p);
	p.tokenbuf = ealloc(p.bufsize);

	RefAdd(pr2);
	input->prompt  = pr1 == NULL ? NULL : pstr("%s", pr1);
	input->prompt2 = pr2 == NULL ? NULL : pstr("%s", pr2);
	RefRemove(pr2);
#if !HAVE_READLINE
	if (input->prompt != NULL)
		eprint("%s", input->prompt);
#endif

	result = yyparse(&p);

	assert(p.ungot == 0);
	if (p.tokenbuf != NULL)
		efree(p.tokenbuf);

	if (result || p.error != NULL) {
		assert(p.error != NULL);
		Ref(const char *, e, str("%s", p.error));
		pseal(NULL);
		setpspace(oldpspace);
		fail("$&parse", "%s", e);
		RefEnd(e);
	}

	Ref(Tree *, tree, pseal(p.tree));
	setpspace(oldpspace);
#if LISPTREES
	if (input->runflags & run_lisptrees)
		eprint("%B\n", tree);
#endif
	RefReturn(tree);
}

/* runinput -- run from an input source */
extern List *runinput(Input *in, int runflags) {
	volatile int flags = runflags;
	List * volatile result = NULL;
	List *repl, *dispatch;
	Push push;
	Input *prev = input;
	const char *dispatcher[] = {
		"fn-%eval-noprint",
		"fn-%eval-print",
		"fn-%noeval-noprint",
		"fn-%noeval-print",
	};

	flags &= ~eval_inchild;
	in->runflags = flags;
	input = in;

	ExceptionHandler

		dispatch
	          = varlookup(dispatcher[((flags & run_printcmds) ? 1 : 0)
					 + ((flags & run_noexec) ? 2 : 0)],
			      NULL);
		if (flags & eval_exitonfalse) {
			dispatch = mklist(mkstr("%exit-on-false"), dispatch);
			flags &= ~eval_exitonfalse;
		}
		varpush(&push, "fn-%dispatch", dispatch);

		repl = varlookup((flags & run_interactive)
				   ? "fn-%interactive-loop"
				   : "fn-%batch-loop",
				 NULL);
		result = (repl == NULL)
				? prim("batchloop", NULL, flags)
				: eval(repl, NULL, flags);

		varpop(&push);

	CatchException (e)

		cleanup(input);
		input = prev;
		throw(e);

	EndExceptionHandler

	cleanup(input);
	input = prev;
	return result;
}


/*
 * pushing new input sources
 */

static void cleanup(Input *in) {
	if (in->fd != -1) {
		unregisterfd(&in->fd);
		close(in->fd);
	}
	efree(in->bufbegin);
}

/* runfd -- run commands from a file descriptor */
extern List *runfd(int fd, const char *name, int flags) {
	Input in;
	List *result;

	memzero(&in, sizeof (Input));
	in.lineno = 1;
	in.fd = fd;
	registerfd(&in.fd, TRUE);
	in.buflen = BUFSIZE;
	in.bufbegin = in.buf = ealloc(in.buflen);
	in.bufend = in.bufbegin;
	in.name = (name == NULL) ? str("fd %d", fd) : name;

	RefAdd(in.name);
	result = runinput(&in, flags);
	RefRemove(in.name);

	return result;
}

/* runstring -- run commands from a string */
extern List *runstring(const char *str, const char *name, int flags) {
	Input in;
	List *result;
	unsigned char *buf;

	assert(str != NULL);

	memzero(&in, sizeof (Input));
	in.fd = -1;
	in.lineno = 1;
	in.name = (name == NULL) ? str : name;
	in.buflen = strlen(str);
	buf = ealloc(in.buflen + 1);
	memcpy(buf, str, in.buflen);
	in.bufbegin = in.buf = buf;
	in.bufend = in.buf + in.buflen;

	RefAdd(in.name);
	result = runinput(&in, flags);
	RefRemove(in.name);
	return result;
}

/* parseinput -- turn an input source into a tree */
extern Tree *parseinput(Input *in) {
	Tree * volatile result = NULL;
	Input *prev = input;

	in->runflags = 0;
	input = in;

	ExceptionHandler
		result = parse(NULL, NULL);
		if (!in->eof)
			fail("$&parse", "more than one value in term");
	CatchException (e)
		cleanup(input);
		input = prev;
		throw(e);
	EndExceptionHandler

	cleanup(input);
	input = prev;
	return result;
}

/* parsestring -- turn a string into a tree; must be exactly one tree */
extern Tree *parsestring(const char *str) {
	Input in;
	Tree *result;
	unsigned char *buf;

	assert(str != NULL);

	/* TODO: abstract out common code with runstring */

	memzero(&in, sizeof (Input));
	in.fd = -1;
	in.lineno = 1;
	in.name = str;
	in.buflen = strlen(str);
	buf = ealloc(in.buflen + 1);
	memcpy(buf, str, in.buflen);
	in.bufbegin = in.buf = buf;
	in.bufend = in.buf + in.buflen;

	RefAdd(in.name);
	result = parseinput(&in);
	RefRemove(in.name);
	return result;
}

/* isinteractive -- is the innermost input source interactive? */
extern Boolean isinteractive(void) {
	return input == NULL ? FALSE : ((input->runflags & run_interactive) != 0);
}

/* isfromfd -- is the innermost input source reading from a file descriptor? */
extern Boolean isfromfd(void) {
	return input == NULL ? FALSE : (input->fd >= 0);
}
