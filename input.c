/* input.c -- read input from files or strings ($Revision: 1.2 $) */
/* stdgetenv is based on the FreeBSD getenv */

#include "es.h"
#include "input.h"


/*
 * constants
 */

#define BUFSIZE     ((size_t) 4096)     /* buffer size to fill reads into */


/*
 * macros
 */

#define ISEOF(in)   ((in)->fill == eoffill)


/*
 * globals
 */

Input *input;
char *prompt, *prompt2;
Boolean resetterminal = FALSE;

#if READLINE
#if HAVE_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#if HAVE_LIBEDIT
#include <editline/readline.h>
#endif

static int rl_initialized = FALSE;

#if ABUSED_GETENV
static char *stdgetenv(const char *);
static char *esgetenv(const char *);
static char *(*realgetenv)(const char *) = stdgetenv;
#endif
#endif /* READLINE */


/*
 * errors and warnings
 */

/* locate -- identify where an error came from */
static char *locate(Input *in, char *s) {
    return (in->runflags & run_interactive)
        ? s
        : str("%s:%d: %s", in->name, in->lineno, s);
}

static char *error = NULL;

/* yyerror -- yacc error entry point */
extern void yyerror(char *s) {
#if sgi
    /* this is so that trip.es works */
    if (streq(s, "Syntax error"))
        s = "syntax error";
#endif
    if (error == NULL)  /* first error is generally the most informative */
        error = locate(input, s);
}

/* warn -- print a warning */
static void warn(char *s) {
    eprint("warning: %s\n", locate(input, s));
}


#define NRUNFLAGS 7
static struct{
    int   mask;
    char *name;
} flagarr[NRUNFLAGS] = {
    {eval_inchild, "inchild"},
    {eval_throwonfalse, "throwonfalse"},
    {run_interactive, "interactive"},
    {run_noexec, "noexec"},
    {run_echoinput, "echoinput"},
    {run_printcmds, "printcmds"},
    {run_lisptrees, "lisptrees"}
};

/* export_runflags -- make runflags into an es list */
static List *export_runflags(int flags) {
    int len = 0;
    char *flagstrs[NRUNFLAGS];

    int i;
    for (i = 0; i < NRUNFLAGS; i++) {
        if (flags & flagarr[i].mask) {
            flagstrs[len++] = flagarr[i].name;
        }
    }

    return listify(len, flagstrs);
}

/*
 * unget -- character pushback
 */

/* ungetfill -- input->fill routine for ungotten characters */
static int ungetfill(Input *in) {
    int c;
    assert(in->ungot > 0);
    c = in->unget[--in->ungot];
    if (in->ungot == 0) {
        assert(in->rfill != NULL);
        in->fill = in->rfill;
        in->rfill = NULL;
        assert(in->rbuf != NULL);
        in->buf = in->rbuf;
        in->rbuf = NULL;
    }
    return c;
}

/* unget -- push back one character */
extern void unget(Input *in, int c) {
    if (in->ungot > 0) {
        assert(in->ungot < MAXUNGET);
        in->unget[in->ungot++] = c;
    } else if (in->bufbegin < in->buf && in->buf[-1] == c && (input->runflags & run_echoinput) == 0)
        --in->buf;
    else {
        assert(in->rfill == NULL);
        in->rfill = in->fill;
        in->fill = ungetfill;
        assert(in->rbuf == NULL);
        in->rbuf = in->buf;
        in->buf = in->bufend;
        assert(in->ungot == 0);
        in->ungot = 1;
        in->unget[0] = c;
    }
}


/*
 * getting characters
 */

/* get -- get a character, filter out nulls */
static int get(Input *in) {
    int c;
    while ((c = (in->buf < in->bufend ? *in->buf++ : (*in->fill)(in))) == '\0')
        warn("null character ignored");
    return c;
}

/* getverbose -- get a character, print it to standard error */
static int getverbose(Input *in) {
    if (in->fill == ungetfill)
        return get(in);
    else {
        int c = get(in);
        if (c != EOF) {
            char buf = c;
            ewrite(2, &buf, 1);
        }
        return c;
    }
}

/* eoffill -- report eof when called to fill input buffer */
static int eoffill(Input *in) {
    assert(in->fd == -1);
    return EOF;
}

#if READLINE
/* callreadline -- readline wrapper */
static char *callreadline(char *prompt) {
    char *r;
    if (prompt == NULL)
        prompt = ""; /* bug fix for readline 2.0 */
    if (!rl_initialized) {
        rl_initialize();
        rl_initialized = TRUE;
    }
    if (resetterminal) {
        rl_reset_terminal(NULL);
        resetterminal = FALSE;
    }
#if HAVE_RL_RESET_SCREEN_SIZE
    rl_reset_screen_size();
#endif
    interrupted = FALSE;
    if (!setjmp(slowlabel)) {
        slow = TRUE;
        r = interrupted ? NULL : readline(prompt);
    } else
        r = NULL;
    slow = FALSE;
    if (r == NULL)
        errno = EINTR;
    SIGCHK();
    return r;
}

#if ABUSED_GETENV

/* getenv -- fake version of getenv for readline (or other libraries) */
static char *esgetenv(const char *name) {
    List *value = varlookup(name, NULL);
    if (value == NULL)
        return NULL;
    else {
        char *export;
        static Dict *envdict;
        static Boolean initialized = FALSE;
        Ref(char *, string, NULL);

        gcdisable();
        if (!initialized) {
            initialized = TRUE;
            envdict = mkdict();
            globalroot(&envdict);
        }

        string = dictget(envdict, name);
        if (string != NULL)
            efree(string);

        export = str("%W", value);
        string = ealloc(strlen(export) + 1);
        strcpy(string, export);
        envdict = dictput(envdict, (char *) name, string);

        gcenable();
        RefReturn(string);
    }
}

static char *
stdgetenv(name)
    register const char *name;
{
    extern char **environ;
    register int len;
    register const char *np;
    register char **p, *c;

    if (name == NULL || environ == NULL)
        return (NULL);
    for (np = name; *np && *np != '='; ++np)
        continue;
    len = np - name;
    for (p = environ; (c = *p) != NULL; ++p)
        if (strncmp(c, name, len) == 0 && c[len] == '=') {
            return (c + len + 1);
        }
    return (NULL);
}

char *
getenv(char *name)
{
    return realgetenv(name);
}

extern void
initgetenv(void)
{
    realgetenv = esgetenv;
}

#endif /* ABUSED_GETENV */

#endif  /* READLINE */

/* fdfill -- fill input buffer by reading from a file descriptor */
static int fdfill(Input *in) {
    long nread;
    assert(in->buf == in->bufend);
    assert(in->fd >= 0);

#if READLINE
    if (in->runflags & run_interactive && in->fd == 0) {
        char *rlinebuf = callreadline(prompt);
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
        nread = eread(in->fd, (char *) in->bufbegin, in->buflen);
        SIGCHK();
    } while (nread == -1 && errno == EINTR);

    if (nread <= 0) {
        close(in->fd);
        in->fd = -1;
        in->fill = eoffill;
        in->runflags &= ~run_interactive;
        if (nread == -1)
            fail("$&parse", "%s: %s", in->name == NULL ? "es" : in->name, esstrerror(errno));
        return EOF;
    }

    if (in->runflags & run_interactive)
        addhistory((char *) in->bufbegin, nread);

    in->buf = in->bufbegin;
    in->bufend = &in->buf[nread];
    return *in->buf++;
}


/*
 * the input loop
 */

/* parse -- call yyparse(), but disable garbage collection and catch errors */
extern Tree *parse(char *pr1, char *pr2) {
    int result;
    assert(error == NULL);
    assert(!pendinghistory());

    inityy();
    emptyherequeue();

    if (ISEOF(input))
        throw(mklist(mkstr("eof"), NULL));

#if READLINE
    prompt = (pr1 == NULL) ? "" : pr1;
#else
    if (pr1 != NULL)
        eprint("%s", pr1);
#endif
    prompt2 = pr2;

    gcreserve(300 * sizeof (Tree));
    gcdisable();
    result = yyparse();
    gcenable();

    if (result || error != NULL) {
        char *e, *h;
        assert(error != NULL);
        e = error;
        error = NULL;

        h = gethistory();

        // Bespoke error-building... :/
        gcdisable();
        Ref(List *, exc, mklist(mkstr("error"),
                         mklist(mkstr("$&parse"),
                         mklist(mkstr(e),
                            (h == NULL
                             ? NULL
                             : mklist(mkstr(str("%s", h)), NULL))))));
        while (gcisblocked())
            gcenable();
        throw(exc);
        RefEnd(exc);
    }
#if LISPTREES
    if (input->runflags & run_lisptrees)
        eprint("%B\n", parsetree);
#endif
    return parsetree;
}

/* resetparser -- clear parser errors in the signal handler */
extern void resetparser(void) {
    char *h;
    if ((h = gethistory()) != NULL)
        efree(h);

    error = NULL;
}

/* runinput -- run from an input source */
extern List *runinput(Input *in, int runflags) {
    volatile int flags = runflags;
    List * volatile result = NULL;
    List *run;

    flags &= ~eval_inchild;
    in->runflags = flags;
    in->get = (flags & run_echoinput) ? getverbose : get;
    in->prev = input;
    input = in;

    ExceptionHandler

        run = varlookup("fn-%run-input", NULL);
        result = (run == NULL)
                    ? prim("batchloop", NULL, NULL, flags)
                    : eval(append(run, export_runflags(flags)), NULL, flags);

    CatchException (e)

        (*input->cleanup)(input);
        input = input->prev;
        throw(e);

    EndExceptionHandler

    input = in->prev;
    (*in->cleanup)(in);
    return result;
}


/*
 * pushing new input sources
 */

/* fdcleanup -- cleanup after running from a file descriptor */
static void fdcleanup(Input *in) {
    unregisterfd(&in->fd);
    if (in->fd != -1)
        close(in->fd);
    efree(in->bufbegin);
}

/* runfd -- run commands from a file descriptor */
extern List *runfd(int fd, const char *name, int flags) {
    Input in;
    List *result;

    memzero(&in, sizeof (Input));
    in.lineno = 1;
    in.fill = fdfill;
    in.cleanup = fdcleanup;
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

/* stringcleanup -- cleanup after running from a string */
static void stringcleanup(Input *in) {
    efree(in->bufbegin);
}

/* stringfill -- placeholder than turns into EOF right away */
static int stringfill(Input *in) {
    in->fill = eoffill;
    return EOF;
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
    in.fill = stringfill;
    in.buflen = strlen(str);
    buf = ealloc(in.buflen + 1);
    memcpy(buf, str, in.buflen);
    in.bufbegin = in.buf = buf;
    in.bufend = in.buf + in.buflen;
    in.cleanup = stringcleanup;

    RefAdd(in.name);
    result = runinput(&in, flags);
    RefRemove(in.name);
    return result;
}

/* parseinput -- turn an input source into a tree */
extern Tree *parseinput(Input *in) {
    Tree * volatile result = NULL;

    in->prev = input;
    in->runflags = 0;
    in->get = get;
    input = in;

    ExceptionHandler
        result = parse(NULL, NULL);
        if (get(in) != EOF)
            fail("$&parse", "more than one value in term");
    CatchException (e)
        (*input->cleanup)(input);
        input = input->prev;
        throw(e);
    EndExceptionHandler

    input = in->prev;
    (*in->cleanup)(in);
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
    in.fill = stringfill;
    in.buflen = strlen(str);
    buf = ealloc(in.buflen + 1);
    memcpy(buf, str, in.buflen);
    in.bufbegin = in.buf = buf;
    in.bufend = in.buf + in.buflen;
    in.cleanup = stringcleanup;

    RefAdd(in.name);
    result = parseinput(&in);
    RefRemove(in.name);
    return result;
}

/* isinteractive -- is the innermost input source interactive? */
extern Boolean isinteractive(void) {
    return input == NULL ? FALSE : ((input->runflags & run_interactive) != 0);
}


#if HAVE_LIBREADLINE
/* Teach readline how to quote a filename in es. "text" is the filename to be
 * quoted. "type" is either SINGLE_MATCH, if there is only one completion
 * match, or MULT_MATCH. "qp" is a pointer to any opening quote character the
 * user typed.
 */
static char *quote(char *text, int type, char *qp) {
	char *p, *r;

	/* worst case: string is entirely quote characters each of which will
	 * be doubled, plus the initial and final quotes and \0 */
	p = r = ealloc(strlen(text) * 2 + 3);
	/* supply opening quote unless already there */
	if (*qp != '\'')
		*p++ = '\'';
	while (*text) {
		if (*text == '\'')
			*p++ = '\''; /* double existing quote */
		*p++ = *text++;
	}
	if (type == SINGLE_MATCH)
		*p++ = '\'';
	*p = '\0';
	return r;
}

/* "unquote" is called with "text", the text of the word to be dequoted, and
 * "quote_char", which is the quoting character that delimits the filename.
 */
char *unquote(char *text, int quote_char) {
	char *p, *r;

	p = r = ealloc(strlen(text) + 1);
	while (*text) {
		*p++ = *text++;
		if (quote_char && *(text - 1) == '\'' && *text == '\'')
			++text;
	}
	*p = '\0';
	return r;
}

static char *complprefix;
static List *(*wordslistgen)(char *);

static char *list_completion_function(const char *text, int state) {
    static char **matches = NULL;
    static int matches_idx;
    static int matches_len;

    const int pfx_len = strlen(complprefix);

    if (!state) {
        const char *name = &text[pfx_len];

        Vector *vm = vectorize(wordslistgen((char *)name));
        matches = vm->vector;
        matches_len = vm->count;
        matches_idx = 0;
    }

    if (!matches || matches_idx >= matches_len)
        return NULL;

    int rlen = strlen(matches[matches_idx]);
    char *result = ealloc(rlen + pfx_len + 1);
    for (int i = 0; i < pfx_len; i++)
        result[i] = complprefix[i];
    strcpy(&result[pfx_len], matches[matches_idx]);
    result[rlen + pfx_len] = '\0';

    matches_idx++;
    return result;
}

char **builtin_completion(const char *text, int start, int end) {
    char **matches = NULL;

    /* TODO: handle more complex things, like `$(a b c)` */
    if (*text == '$') {
        wordslistgen = varswithprefix;
        complprefix = "$";
        switch (text[1]) {
        case '&':
            wordslistgen = primswithprefix;
            complprefix = "$&";
            break;
        case '^': complprefix = "$^"; break;
        case '#': complprefix = "$#"; break;
        }
        matches = rl_completion_matches(text, list_completion_function);
    }

    /* ~foo => username.  ~foo/bar already gets completed as filename. */
    /* TODO: should this deglob the dir as well? i.e., ~foo => /home/foo? */
    if (!matches && *text == '~' && !strchr(text, '/'))
        matches = rl_completion_matches(text, rl_username_completion_function);

    /* TODO: basic commands????       */
    /* TODO: globbing filenames?????? */

    return matches;
}
#endif

/*
 * initialization
 */

/* initinput -- called at dawn of time from main() */
extern void initinput(void) {
    input = NULL;

    /* declare the global roots */
    globalroot(&error);         /* parse errors */
    globalroot(&prompt);        /* main prompt */
    globalroot(&prompt2);       /* secondary prompt */

    /* call the parser's initialization */
    initparse();

#if READLINE
    /* call history initialization */
    inithistory();

    rl_readline_name = "es";

    /* TODO: re-insert '&' in these lists, or fake it in the right spot in
     * builtin_completion. It's currently absent here to allow for primitive
     * completion. */
    rl_completer_word_break_characters = " \t\n\\`$><=;|{()}";
    rl_basic_word_break_characters = " \t\n\\'`$><=;|{()}";
    rl_completer_quote_characters = "'";
    rl_special_prefixes = "$";

    /* rl_instream = stdin;
    rl_outstream = stderr; */

#if HAVE_LIBREADLINE
    /* TODO: technically, libedit can handle pretty much everything in
     * builtin_completion.  but it's just a bit more work to make the code
     * properly generic. */
    rl_attempted_completion_function = builtin_completion;

    rl_filename_quote_characters = " \t\n\\`$><=;|&{()}";
    rl_filename_quoting_function = quote;
    rl_filename_dequoting_function = unquote;
#endif
#endif
}
