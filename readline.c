/* readline.c -- readline primitives */

#include "es.h"
#include "prim.h"

#if HAVE_READLINE

#include <readline/readline.h>
#include <readline/history.h>


/*
 * globals
 */

static Boolean reloadhistory = FALSE;
static Boolean resetterminal = FALSE;
static char *history = NULL;

#if 0
/* These split history file entries by timestamp, which allows readline to pick up
 * multi-line commands correctly across process boundaries.  Disabled by default,
 * because it leaves the history file itself kind of ugly. */
static int history_write_timestamps = 1;
static char history_comment_char = '#';
#endif


/*
 * history functions
 */

static int sethistorylength = -1; /* unlimited */

extern void setmaxhistorylength(int len) {
	sethistorylength = len;
}

extern void loghistory(char *cmd) {
	int err;
	if (cmd == NULL)
		return;
	add_history(cmd);
	if (history == NULL)
		return;

	if ((err = append_history(1, history))) {
		eprint("history(%s): %s\n", history, esstrerror(err));
		vardef("history", NULL, NULL);
	}
}

static int count_history(void) {
	int i, n, count = 0, fd = eopen(history, oOpen);
	char buf[4096];
	if (fd < 0)
		return -1;
	while ((n = read(fd, &buf, 4096)) != 0) {
		if (n < 0) {
			if (errno == EINTR) {
				SIGCHK();
				continue;
			} else {
				close(fd);
				return -1;
			}
		}
		for (i = 0; i < n; i++)
			if (buf[i] == '\n')
				count++;
	}
	close(fd);
	return count;
}

static void reload_history(void) {
	/* Attempt to populate readline history with new history file. */
	if (history != NULL) {
		int n = count_history() - sethistorylength;
		if (sethistorylength < 0 || n < 0) n = 0;
		read_history_range(history, n, -1);
	}
	using_history();

	reloadhistory = FALSE;
}

static void inithistory(void) {
	static Boolean initialized = FALSE;
	if (initialized)
		return;
	globalroot(&history);
	initialized = TRUE;
}

extern void sethistory(char *file) {
	inithistory();
	if (reloadhistory)
		reload_history();
	reloadhistory = TRUE;
	history = file;
}

extern void checkhistory(void) {
	static int effectivelength = -1;
	if (reloadhistory)
		reload_history();
	if (sethistorylength != effectivelength) {
		switch (sethistorylength) {
		case -1:
			unstifle_history();
			break;
		case 0:
			clear_history();
			FALLTHROUGH;
		default:
			stifle_history(sethistorylength);
		}
		effectivelength = sethistorylength;
	}
}


/*
 * readline functions
 */

/* quote -- teach readline how to quote a word during completion.
 * prefix is prepended _before_ the quotes, such as: $'foo bar' */
static char *quote(char *text, Boolean open, char *prefix, char *qp) {
	char *quoted;
	if (*qp != '\0' || strpbrk(text, rl_filename_quote_characters)) {
		quoted = mprint("%s%#S", prefix, text);
		if (open)
			quoted[strlen(quoted)-1] = '\0';
	} else {
		quoted = mprint("%s%s", prefix, text);
	}
	efree(text);
	return quoted;
}

/* unquote -- remove quotes from text and point *qp at the relevant quote char */
static char *unquote(const char *text, char **qp) {
	char *p, *r;
	Boolean quoted = FALSE;

	p = r = ealloc(strlen(text) + 1);
	while ((*p = *text++)) {
		if (*p == '\'') {
			if (quoted && *text == '\'') {
				p++;
				text++;
			} else {
				quoted = !quoted;
				if (quoted && qp != NULL)
					*qp = p;
			}
		} else if (!quoted && *p == '\\') {
			/* anything else won't be handled correctly by the completer */
			if (*text == ' ' || *text == '\'')
				*p++ = *text++;
		} else
			p++;
	}
	*p = '\0';
	if (!quoted && qp != NULL)
		*qp = p;
	return r;
}

/* Unquote files to allow readline to detect which are directories. */
static int unquote_for_stat(char **name) {
	char *unquoted;
	if (!strpbrk(*name, rl_filename_quote_characters))
		return 0;

	unquoted = unquote(*name, NULL);
	efree(*name);
	*name = unquoted;
	return 1;
}

/* Find the start of the word to complete.  This uses the trick where we set rl_point
 * to the start of the word to indicate the start of the word.  For this to work,
 * rl_basic_quote_characters must be the empty string or else this function's result
 * is overwritten, and doing that means we have to reimplement basically all quoting
 * behavior manually. */
static char *completion_start(void) {
	int i, start = 0;
	Boolean quoted = FALSE, backslash = FALSE;
	for (i = 0; i < rl_point; i++) {
		char c = rl_line_buffer[i];
		if (backslash) {
			backslash = FALSE;
			continue;
		}
		if (c == '\'')
			quoted = !quoted;
		else if (!quoted && c == '\\')
			backslash = TRUE;
		else if (!quoted && strchr(rl_basic_word_break_characters, c))
			start = i; /* keep possible '$' char in term */
	}
	rl_point = start;
	return NULL;
}

/* Basic function to use an es List created by gen() to generate readline matches. */
static char *list_completion(const char *text, int state, List *(*gen)(const char *)) {
	static char **matches = NULL;
	static int i, len;

	if (!state) {
		Vector *vm = vectorize(gen(text));
		matches = vm->vector;
		len = vm->count;
		i = 0;
	}

	if (!matches || i >= len)
		return NULL;

	return mprint("%s", matches[i++]);
}

static char *var_completion(const char *text, int state) {
	return list_completion(text, state, varswithprefix);
}

static char *prim_completion(const char *text, int state) {
	return list_completion(text, state, primswithprefix);
}

typedef enum {
	NORMAL,
	SYNTAX_ERROR,
	FDBRACES
} CompletionType;

/* hmm. */
extern const char nw[];

/* Scan line back to its start. */
/* This is a lot of code, and a poor reimplementation of the parser. :( */
CompletionType boundcmd(char **start) {
	char *line = rl_line_buffer;
	char syntax[128] = { 0 };
	int lp, sp = 0;
	Boolean quote = FALSE, first_word = TRUE;

	for (lp = rl_point; lp > 0; lp--) {
		if (quote)
			continue;

		switch (line[lp]) {
		/* quotes. pretty easy */
		case '\'':
			quote = !quote;
			continue;

		/* "stackable" syntax.  remember, we're moving backwards */
		case '}':
			syntax[sp++] = '{';
			break;
		case '{':
			if (sp == 0) {
				*start = rl_line_buffer + lp + 1;
				return NORMAL;
			}
			if (syntax[--sp] != '{') {
				*start = rl_line_buffer;
				return SYNTAX_ERROR;
			}
			break;
		case ')':
			syntax[sp++] = '(';
			break;
		case '(':
			if (sp > 0) {
				if (syntax[--sp] != '(') {
					*start = rl_line_buffer;
					return SYNTAX_ERROR;
				}
			} else {
				/* TODO: make `<=(a b` work */
				first_word = TRUE;
			}
			break;

		/* command separator chars */
		case ';':
			if (sp == 0) {
				*start = rl_line_buffer + lp + 1;
				return NORMAL;
			}
			break;
		case '&':
			if (sp == 0) {
				*start = rl_line_buffer + lp + 1;
				return NORMAL;
			}
			break;
		case '|':
			if (sp == 0) {
				int pp = lp+1;
				Boolean inbraces = FALSE;
				if (pp < rl_point && line[pp] == '[') {
					inbraces = TRUE;
					while (pp < rl_point) {
						if (line[pp++] == ']') {
							inbraces = FALSE;
							break;
						}
					}
				}
				*start = rl_line_buffer + pp;
				return inbraces ? FDBRACES : NORMAL;
			}
			break;
		case '`':
			if (first_word) {
				*start = rl_line_buffer + lp + 1;
				return NORMAL;
			}
			break;
		case '<':
			if (first_word && lp < rl_point - 1 && line[lp+1] == '=') {
				*start = rl_line_buffer + lp + 2;
				return NORMAL;
			}
			break;
		}
		if (nw[(unsigned char)line[lp]])
			first_word = FALSE;
	}
	/* TODO: fetch previous lines if sp > 0 */
	*start = rl_line_buffer;
	return NORMAL;
}

/* calls `%complete prefix word` to get a list of candidates for how to complete
 * `word`.
 *
 * TODO: improve argv for %complete
 *  - special dispatch for special syntax
 *  - split up args in a syntax-aware way
 *  - dequote args before and requote after (already done, just do it better)
 *  - skip/handle "command-irrelevant" syntax
 *      ! redirections binders
 *  - MAYBE: provide raw command/point?
 *
 * all the new behaviors above should ideally be done "manually", so that %complete
 * can be used the same way without worrying about the line editing library.
 *
 * Handle the following properly, though maybe not in this function
 *   `let (a =`
 *   `let (a = b)`
 *   `a =`
 *   `a > `
 *   `!`
 *   `$(f`
 */

static List *callcomplete(const char *word) {
	int len;
	char *start;
	CompletionType type;

	Ref(List *, result, NULL);
	Ref(List *, fn, NULL);
	if ((fn = varlookup("fn-%complete", NULL)) == NULL) {
		RefPop(fn);
		return NULL;
	}
	type = boundcmd(&start);

	if (type == FDBRACES) {
		/* TODO: fd completion */
		RefPop2(result, fn);
		return NULL;
	}

	len = rl_point - (start - rl_line_buffer) - strlen(word);
	if (len < 0) {	/* TODO: fix `word` for `|[2]` and delete this hack */
		len = 0;
		word = "";
	}
	Ref(char *, line, gcndup(start, len));
	gcdisable();
	fn = append(fn, mklist(mkstr(line),
				mklist(mkstr(str("%s", word)), NULL)));
	gcenable();
	result = eval(fn, NULL, 0);
	RefEnd2(line, fn);
	RefReturn(result);
}

static char *es_completion(const char *text, int state) {
	return list_completion(text, state, callcomplete);
}

List *completion_to_file;

/* TODO: does callcompletiontofile perform unquoting/requoting correctly? */
static int callcompletiontofile(char **filep) {
	List *result;
	if (completion_to_file == NULL)
		return 0;
	Ref(List *, call, NULL);
	gcdisable();
	call = append(completion_to_file, mklist(mkstr(*filep), NULL));
	gcenable();
	result = eval(call, NULL, 0);
	RefEnd(call);
	switch (length(result)) {
	case 0:
		return 0;
	case 1:
		if (streq(*filep, getstr(result->term)))
			return 0;
		/* move into ealloc-space */
		*filep = mprint("%E", result->term);
		return 1;
	default:
		fail("%completion-to-file", "completion-filename mapping must return one value");
	}
}

static int matchcmp(const void *a, const void *b) {
	return strcoll(*(const char **)a, *(const char **)b);
}

/* Pick out a completion to perform based on the string's prefix */
rl_compentry_func_t *select_completion(const char *text, char **prefix) {
	if (*text == '$') {
		switch (text[1]) {
		case '&':
			*prefix = "$&";
			return prim_completion;
		case '^': *prefix = "$^"; break;
		case '#': *prefix = "$#"; break;
		default:  *prefix = "$";
		}
		return var_completion;
	} else if (*text == '~' && !strchr(text, '/')) {
		/* ~foo => username.  ~foo/bar gets completed as a filename. */
		return rl_username_completion_function;
	}
	return es_completion;
}

static rl_compentry_func_t *completion_func = NULL;

/* Top-level completion function.  If completion_func is set, performs that completion.
 * Otherwise, performs a completion based on the prefix of the text. */
char **builtin_completion(const char *text, int UNUSED start, int UNUSED end) {
	char **matches = NULL, *qp = NULL, *prefix = "";
	Push ctf;
	/* Manually unquote the text, since we told readline not to. */
	char *t = unquote(text, &qp);
	rl_compentry_func_t *completion;

	if (completion_func != NULL) {
		completion = completion_func;
		completion_func = NULL;
	} else
		completion = select_completion(text, &prefix);

	if (completion == es_completion)
		varpush(&ctf, "fn-%completion-to-file", NULL);

	matches = rl_completion_matches(t+strlen(prefix), completion);

	if (completion == es_completion) {
		completion_to_file = varlookup("fn-%completion-to-file", NULL);
		if (completion_to_file != NULL)
			rl_filename_completion_desired = 1;
		varpop(&ctf);
	}

	/* Manually sort and then re-quote the matches. */
	if (matches != NULL) {
		size_t i, n;
		for (n = 1; matches[n]; n++)
			;
		qsort(&matches[1], n - 1, sizeof(matches[0]), matchcmp);
		matches[0] = quote(matches[0], n > 1, prefix, qp);
		for (i = 1; i < n; i++)
			matches[i] = quote(matches[i], FALSE, prefix, qp);
	}

	efree(t);

	/* Since we had to sort and quote results ourselves, we disable the automatic
	 * filename completion and sorting. */
	rl_attempted_completion_over = 1;
	rl_sort_completion_matches = 0;
	return matches;
}

/* Unquote matches when displaying in a menu.  This wouldn't be necessary, if not for
 * menu-complete. */
static void display_matches(char **matches, int num, int max) {
	int i;
	char **unquoted;

	if (rl_completion_query_items > 0 && num >= rl_completion_query_items) {
		int c;
		rl_crlf();
		fprintf(rl_outstream, "Display all %d possibilities? (y or n)", num);
		fflush(rl_outstream);
		c = rl_read_key();
		if (c != 'y' && c != 'Y' && c != ' ') {
			rl_crlf();
			rl_forced_update_display();
			return;
		}
	}

	unquoted = ealloc(sizeof(char *) * (num + 2));
	for (i = 0; matches[i]; i++)
		unquoted[i] = unquote(matches[i], NULL);
	unquoted[i] = NULL;

	rl_display_match_list(unquoted, num, max);
	rl_forced_update_display();

	for (i = 0; unquoted[i]; i++)
		efree(unquoted[i]);
	efree(unquoted);
}

static int es_complete_filename(int UNUSED count, int UNUSED key) {
	completion_func = rl_filename_completion_function;
	return rl_complete_internal(rl_completion_mode(es_complete_filename));
}

static int es_complete_variable(int UNUSED count, int UNUSED key) {
	completion_func = var_completion;
	return rl_complete_internal(rl_completion_mode(es_complete_variable));
}

static int es_complete_primitive(int UNUSED count, int UNUSED key) {
	completion_func = prim_completion;
	return rl_complete_internal(rl_completion_mode(es_complete_primitive));
}

static void initreadline(void) {
	globalroot(&completion_to_file);

	rl_readline_name = "es";

	/* this word_break_characters excludes '&' due to primitive completion */
	rl_basic_word_break_characters = " \t\n`$><=;|{()}";
	rl_filename_quote_characters = " \t\n\\`'$><=;|&{()}";
	rl_basic_quote_characters = "";
	rl_special_prefixes = "$";

	rl_completion_word_break_hook = completion_start;
	rl_filename_stat_hook = unquote_for_stat;
	rl_attempted_completion_function = builtin_completion;
	rl_completion_display_matches_hook = display_matches;
	rl_directory_rewrite_hook = callcompletiontofile;
	rl_filename_stat_hook = callcompletiontofile;

	rl_add_funmap_entry("es-complete-filename", es_complete_filename);
	rl_add_funmap_entry("es-complete-variable", es_complete_variable);
	rl_add_funmap_entry("es-complete-primitive", es_complete_primitive);
	rl_bind_keyseq("\033/", es_complete_filename);
	rl_bind_keyseq("\033$", es_complete_variable);
}

static void prepreadline(void) {
	static Boolean initialized = FALSE;
	if (!initialized) {
		initreadline();
		initialized = TRUE;
	}
	checkhistory();
	if (resetterminal) {
		rl_reset_terminal(NULL);
		resetterminal = FALSE;
	}
	if (RL_ISSTATE(RL_STATE_INITIALIZED))
		rl_reset_screen_size();
}

/* callreadline -- readline wrapper */
static char *callreadline(char *prompt0) {
	char *r;
	Ref(char *volatile, prompt, prompt0);
	prepreadline();
	if (prompt == NULL)
		prompt = ""; /* bug fix for readline 2.0 */
	if (!sigsetjmp(slowlabel, 1)) {
		slow = TRUE;
		r = readline(prompt);
	} else {
		r = NULL;
		errno = EINTR;
	}
	slow = FALSE;
	SIGCHK();
	RefEnd(prompt);
	return r;
}

static FILE *fdmapopen(int fd, const char *mode) {
	FILE *f;
	if ((fd = dup(fdmap(fd))) == -1)
		fail("$&readline", "dup: %s", esstrerror(errno));
	if ((f = fdopen(fd, mode)) == NULL) {
		int err = errno;
		close(fd);
		fail("$&readline", "fdopen: %s", esstrerror(err));
	}
	return f;
}


/*
 * primitive interface
 */

PRIM(readline) {
	char *line;
	char *prompt = (list == NULL ? "" : getstr(list->term));
	if (list != NULL && list->next != NULL)
		fail("$&readline", "usage: %read-line [prompt]");

	if (!isatty(fdmap(0))) {
		list = prim("read", NULL, 0);
		if (length(list) <= 1)
			return list;
		return mklist(mkstr(str("%L", list, "")), NULL);
	}

	rl_instream = fdmapopen(0, "r");
	ExceptionHandler
		rl_outstream = fdmapopen(2, "w");
	CatchException (e)
		fclose(rl_instream);
		throw(e);
	EndExceptionHandler

	ExceptionHandler

		do {
			line = callreadline(prompt);
		} while (line == NULL && errno == EINTR);

	CatchException (e)

		fclose(rl_instream);
		fclose(rl_outstream);
		throw(e);

	EndExceptionHandler

	fclose(rl_instream);
	fclose(rl_outstream);

	if (line == NULL)
		return NULL;
	list = mklist(mkstr(str("%s", line)), NULL);
	efree(line);
	return list;
}

PRIM(sethistory) {
	if (list == NULL) {
		sethistory(NULL);
		return NULL;
	}
	Ref(List *, lp, list);
	sethistory(getstr(lp->term));
	RefReturn(lp);
}

PRIM(writehistory) {
	if (list == NULL || list->next != NULL)
		fail("$&writehistory", "usage: $&writehistory command");
	loghistory(getstr(list->term));
	return NULL;
}

/* limitations here include: can't tell when it only removed escape characters,
 * doesn't understand es quote/escape rules */
PRIM(historyexpand) {
	char *buf;
	List *result;
	if (list == NULL)
		return NULL;
	if (history_expand(getstr(list->term), &buf) == -1)
		fail("$&historyexpand", "%s", buf); /* this leaks buf */
	result = mklist(mkstr(str("%s", buf)), NULL);
	efree(buf);
	return result;
}

PRIM(setmaxhistorylength) {
	char *s;
	int n;
	if (list == NULL) {
		setmaxhistorylength(-1); /* unlimited */
		return NULL;
	}
	if (list->next != NULL)
		fail("$&setmaxhistorylength", "usage: $&setmaxhistorylength [limit]");
	Ref(List *, lp, list);
	n = (int)strtol(getstr(lp->term), &s, 0);
	if (n < 0 || (s != NULL && *s != '\0'))
		fail("$&setmaxhistorylength", "max-history-length must be set to a positive integer");
	setmaxhistorylength(n);
	RefReturn(lp);
}

PRIM(resetterminal) {
	resetterminal = TRUE;
	return ltrue;
}

/* hooking a key binding to an es command is funky.  TWO bindings are created:
 *  1. rl_bind_keyseq(kseq, rl_dispatch) => call es_rl_dispatch for any
 *     es-command-bound key
 *  2. rl_generic_bind(ISMACR, kseq, cmd, eskeymap) => create a mapping for
 *     es_rl_dispatch to "manually" look up which cmd to call
 *
 * eskeymap is not GC'd, so we store an mprinted string version of our command
 * there.
 */
Keymap eskeymap = NULL;

static int es_rl_dispatch(int UNUSED count, int UNUSED key) {
	int type;
	char *cmd = (char *)rl_function_of_keyseq(rl_executing_keyseq, eskeymap, &type);

	if (cmd == NULL || type != ISMACR)
		fail("$&readline", "cannot find command for key sequence");

	/* TODO: decide what the actual call environment is for cmd.
	 * arguments? environment? stdout? return value? */
	eval(mklist(mkstr(str("{%s}", cmd)), NULL), NULL, 0);	/* TODO: evalflags? */
	return 0;
}

PRIM(setrlhook) {
	char *kseq;
	if (list == NULL)
		fail("$&rlhook", "usage: $&setrlhook keyseq [cmd]");
	if (eskeymap == NULL)
		eskeymap = rl_make_bare_keymap();
	Ref(List *, lp, list->next);
	kseq = getstr(list->term);	/* bogus keymap?? */
	if (list->next != NULL) {
		/* set it */
		char *cmd = mprint("%L", lp, " ");
		rl_bind_keyseq(kseq, es_rl_dispatch);
		rl_generic_bind(ISMACR, kseq, cmd, eskeymap);
	} else {
		/* forget it
		 * FIXME: memory leaks
		 * FIXME: I don't know if this "unsets" in the way I expect
		 *        do I want NULL or do I want whatever the default is?
		 */
		rl_bind_keyseq(kseq, NULL);
		rl_bind_keyseq_in_map(kseq, NULL, eskeymap);
	}
	RefReturn(lp);
}

extern Dict *initprims_readline(Dict *primdict) {
	X(readline);
	X(sethistory);
	X(writehistory);
	X(historyexpand);
	X(resetterminal);
	X(setmaxhistorylength);
	X(setrlhook);
	return primdict;
}
#endif
