/* token.c -- lexical analyzer for es ($Revision: 1.1.1.1 $) */

#include "es.h"
#include "input.h"
#include "syntax.h"
#include "token.h"

#define isodigit(c) ('0' <= (c) && (c) < '8')

#define BUFSIZE ((size_t) 2048)
#define BUFMAX  (8 * BUFSIZE)

typedef enum { NW, RW, KW } State;  /* "nonword", "realword", "keyword" */

static State w = NW;
Boolean numeric = FALSE;
static Boolean newline = FALSE;
static Boolean goterror = FALSE;
static Boolean skipequals = FALSE;
static Boolean skipdual = FALSE;
static size_t bufsize = 0;
static char *tokenbuf = NULL;

#define InsertFreeCaret()   STMT(if (w != NW) { w = NW; UNGETC(c); return '^'; })


extern void setskip(Boolean dual) {
    skipequals = TRUE;
    if (dual) skipdual = TRUE;
}

extern void unsetskip(void) {
    skipequals = FALSE;
    skipdual = FALSE;
}

/*
 *  Special characters (i.e., "non-word") in es:
 *      \t \n # ; & | ^ $ = ` ' ! { } ( ) < > \
 */

/* Non-word in default context */
const char nw[] = {
    1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0,     /*   0 -  15 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /*  16 -  32 */
    1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,     /* ' ' - '/' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0,     /* '0' - '?' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* '@' - 'O' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0,     /* 'P' - '_' */
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* '`' - 'o' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0,     /* 'p' - DEL */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* 128 - 143 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* 144 - 159 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* 160 - 175 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* 176 - 191 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* 192 - 207 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* 208 - 223 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* 224 - 239 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* 240 - 255 */
};

/* Non-word in numeric context */
const char nnw[] = {
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 0.  0 -  15 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 1.  16 -  32 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1,     /* 2. ' ' - '/' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,     /* 3. '0' - '?' */
    1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 4. '@' - 'O' */
    1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,     /* 5. 'P' - '_' */
    1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 6. '`' - 'o' */
    1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,     /* 7. 'p' - DEL */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 8. 128 - 143 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 9. 144 - 159 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* A. 160 - 175 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* B. 176 - 191 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* C. 192 - 207 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* D. 208 - 223 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* E. 224 - 239 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* F. 240 - 255 */
};

/* "dollar non-word", i.e., invalid variable characters */
/* N.B. valid chars are % * - [0-9] [A-Z] _ [a-z]       */
const char dnw[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /*   0 -  15 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /*  16 -  32 */
    1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1,     /* ' ' - '/' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,     /* '0' - '?' */
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* '@' - 'O' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0,     /* 'P' - '_' */
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* '`' - 'o' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,     /* 'p' - DEL */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 128 - 143 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 144 - 159 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 160 - 175 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 176 - 191 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 192 - 207 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 208 - 223 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 224 - 239 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,     /* 240 - 255 */
};

/* print_prompt2 -- called before all continuation lines */
extern void print_prompt2(void) {
    input->lineno++;
#if READLINE
    prompt = prompt2;
#else
    if ((input->runflags & run_interactive) && prompt2 != NULL)
        eprint("%s", prompt2);
#endif
}

/* scanerror -- called for lexical errors */
static void scanerror(int last, char *s) {
    int c;
    if (last != '\n' && last != EOF)
        while ((c = GETC()) != '\n' && c != EOF)
            ;
    goterror = TRUE;
    yyerror(s);
}

/*
 * getfds
 *  Scan in a pair of integers for redirections like >[2=1]. CLOSED represents
 *  a closed file descriptor (i.e., >[2=]).
 *
 *  This function makes use of unsigned compares to make range tests in
 *  one compare operation.
 */

#define CLOSED  -1
#define DEFAULT -2

static Boolean getfds(int fd[2], int c, int default0, int default1) {
    int n;
    fd[0] = default0;
    fd[1] = default1;

    if (c != '[') {
        UNGETC(c);
        return TRUE;
    }
    if ((unsigned int) (n = GETC() - '0') > 9) {
        scanerror(n + '0', "expected digit after '['");
        return FALSE;
    }

    while ((unsigned int) (c = GETC() - '0') <= 9)
        n = n * 10 + c;
    fd[0] = n;

    switch (c += '0') {
    case '=':
        if ((unsigned int) (n = GETC() - '0') > 9) {
            if (n != ']' - '0') {
                scanerror(n + '0', "expected digit or ']' after '='");
                return FALSE;
            }
            fd[1] = CLOSED;
        } else {
            while ((unsigned int) (c = GETC() - '0') <= 9)
                n = n * 10 + c;
            if (c != ']' - '0') {
                scanerror(c + '0', "expected ']' after digit");
                return FALSE;
            }
            fd[1] = n;
        }
        break;
    case ']':
        break;
    default:
        scanerror(c, "expected '=' or ']' after digit");
        return FALSE;
    }
    return TRUE;
}

static Boolean skip(Boolean dual, int c) {
    Boolean skipeq = dual ? skipdual : skipequals;
    if (!skipeq || c != '=') return FALSE;
    Boolean res = ((c = GETC()) != '>');
    UNGETC(c);
    return res;
}

static inline void bufput(char **buf, int pos, char val) {
    while ((unsigned long)pos >= bufsize) {
        *buf = tokenbuf = erealloc(tokenbuf, bufsize *= 2);
    }
    (*buf)[pos] = val;
}

extern int yylex(void) {
    if (goterror) {
        goterror = FALSE;
        return NL;
    }

    if (newline) {
        --input->lineno; /* slight space optimization; print_prompt2() always increments lineno */
        print_prompt2();
        newline = FALSE;
    }

    static Boolean dollar = FALSE;
    int c;
    size_t i;                   /* The purpose of all these local assignments is to     */
    const char *meta;           /* allow optimizing compilers like gcc to load these    */
    char *buf = tokenbuf;       /* values into registers. On a sparc this is a          */
    YYSTYPE *y = &yylval;       /* win, in code size *and* execution time               */

    /* set the right set of non-word characters based on the current mode */
    meta = (numeric ? nnw : dollar ? dnw : nw);
    dollar = FALSE;

top:
    while ((c = GETC()) == ' ' || c == '\t')
        w = NW;

    if (c == EOF)
        return ENDFILE;

    if ((!meta[(unsigned char) c] || skip(TRUE, c))) {    /* it's a word or keyword. */
        InsertFreeCaret();
        w = RW;
        i = 0;
        do {
            bufput(&buf, i++, c);
        } while ((c = GETC()) != EOF &&
                 (!meta[(unsigned char) c] || skip(FALSE, c)));
        UNGETC(c);
        bufput(&buf, i, '\0');

        if (numeric) {
            errno = 0;
            char *end;
            es_int_t ival = STR_TO_EI(buf, &end);
            if (*end == '\0') {
                if (errno == ERANGE) {
                    scanerror(c, "integer value too large");
                    return ERROR;
                }
                y->ival = ival;
                return INT;
            }

            es_float_t fval = STR_TO_EF(buf, &end);
            if (*end == '\0') {
                if (errno == ERANGE) {
                    scanerror(c, "float value too large");
                    return ERROR;
                }
                y->fval = fval;
                return FLOAT;
            }

            scanerror(c, "invalid number");
            return ERROR;
        }

        w = KW;
        setskip(FALSE);
        if (buf[1] == '\0') {
            int k = *buf;
            if (k == '@' || k == '~')
                return k;
        } else if (*buf == 'f') {
            if (streq(buf + 1, "n"))    return FN;
            if (streq(buf + 1, "or"))   return FOR;
        } else if (*buf == 'l') {
            if (streq(buf + 1, "ocal")) return LOCAL;
            if (streq(buf + 1, "et"))   return LET;
        } else if (streq(buf, "~~"))
            return EXTRACT;
        else if (streq(buf, "%closure"))
            return CLOSURE;
        w = RW;
        y->str = gcdup(buf);
        return WORD;
    }

    if (numeric) {
        switch (c) {
        case '!':
            w = NW;
            c = GETC();
            if (c == '=')
                return NEQ;
            UNGETC(c);
            c = '!';
            break;
        case '<': case '=': case '>': {
            w = NW;
            char cmp = c;
            c = GETC();
            if (c == '=') {
                switch (cmp) {
                case '<': return LEQ;
                case '=': return '=';
                case '>': return GEQ;
                }
            } else {
                UNGETC(c);
                return cmp;
            }
        }
        case '-': case '+': case '*': case '/': case '%':
            w = NW;
            return c;
        }
    }

    if (c == '`' || c == '!' || c == '$' || c == '\'') {
        InsertFreeCaret();
        if (c == '!')
            w = KW;
    }

    switch (c) {
    case '!':
        return '!';
    case '`':
        c = GETC();
        numeric = FALSE;
        if (c == '`')
            return BACKBACK;
        if (c == '(') {
            numeric = TRUE;
            return ARITH_BEGIN;
        }
        UNGETC(c);
        return '`';
    case '$':
        dollar = TRUE;
        numeric = FALSE;
        switch (c = GETC()) {
        case '#':   return COUNT;
        case '^':   return FLAT;
        case '&':   return PRIM;
        default:    UNGETC(c); return '$';
        }
    case '\'':
        w = RW;
        i = 0;
        while ((c = GETC()) != '\'' || (c = GETC()) == '\'') {
            bufput(&buf, i++, c);
            if (c == '\n')
                print_prompt2();
            if (c == EOF) {
                w = NW;
                scanerror(c, "eof in quoted string");
                return ERROR;
            }
        }
        UNGETC(c);
        bufput(&buf, i, '\0');
        setskip(FALSE);
        y->str = gcdup(buf);
        return QWORD;
    case '\\':
        if ((c = GETC()) == '\n') {
            print_prompt2();
            UNGETC(' ');
            goto top; /* Pretend it was just another space. */
        }
        if (c == EOF) {
            UNGETC(EOF);
            goto badescape;
        }
        UNGETC(c);
        c = '\\';
        InsertFreeCaret();
        w = RW;
        c = GETC();
        switch (c) {
        case 'a':   *buf = '\a';    break;
        case 'b':   *buf = '\b';    break;
        case 'e':   *buf = '\033';  break;
        case 'f':   *buf = '\f';    break;
        case 'n':   *buf = '\n';    break;
        case 'r':   *buf = '\r';    break;
        case 't':   *buf = '\t';    break;
        case 'x': case 'X': {
            int n = 0;
            for (;;) {
                c = GETC();
                if (!isxdigit(c))
                    break;
                n = (n << 4)
                  | (c - (isdigit(c) ? '0' : ((islower(c) ? 'a' : 'A') - 0xA)));
            }
            if (n == 0)
                goto badescape;
            UNGETC(c);
            *buf = n;
            break;
        }
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
            int n = 0;
            do {
                n = (n << 3) | (c - '0');
                c = GETC();
            } while (isodigit(c));
            if (n == 0)
                goto badescape;
            UNGETC(c);
            *buf = n;
            break;
        }
        default:
            if (isalnum(c)) {
            badescape:
                scanerror(c, "bad backslash escape");
                return ERROR;
            }
            *buf = c;
            break;
        }
        buf[1] = 0;
        y->str = gcdup(buf);
        setskip(FALSE);
        return QWORD;
    case '#':
        while ((c = GETC()) != '\n') /* skip comment until newline */
            if (c == EOF)
                return ENDFILE;
        /* FALLTHROUGH */
    case '\n':
        input->lineno++;
        newline = TRUE;
        unsetskip();
        w = NW;
        return NL;
    case '(':
        if (w == RW)    /* not keywords, so let & friends work */
            c = SUB;
        /* nb: IF we change parse.y such that 'nl' no longer implies 'unsetskip()',
         * or if 'nl' no longer necessarily comes between 'binder' and '(', we need
         * to potentially call 'unsetskip()' here, if binder is the previous nonterminal. */
        w = NW;
        return c;
    case ';':
    case '{':
        unsetskip();
        /* FALLTHROUGH */
    case ')':
    case '}':
    case '^':
        w = NW;
        return c;
    case '=':
        w = NW;
        c = GETC();
        if (c == '>') {
            unsetskip();
            return PASS;
        }
        UNGETC(c);
        setskip(TRUE);
        return '=';
    case '&':
        w = NW;
        unsetskip();
        c = GETC();
        if (c == '&')
            return ANDAND;
        UNGETC(c);
        return '&';

    case '|': {
        int p[2];
        w = NW;
        unsetskip();
        c = GETC();
        if (c == '|')
            return OROR;
        if (!getfds(p, c, 1, 0))
            return ERROR;
        if (p[1] == CLOSED) {
            scanerror(c, "expected digit after '='");  /* can't close a pipe */
            return ERROR;
        }
        y->tree = mk(nPipe, p[0], p[1]);
        return PIPE;
    }

    {
        char *cmd;
        int fd[2];
    case '<':
        fd[0] = 0;
        if ((c = GETC()) == '>')
            if ((c = GETC()) == '>') {
                c = GETC();
                cmd = "%open-append";
            } else
                cmd = "%open-write";
        else if (c == '<')
            if ((c = GETC()) == '<') {
                c = GETC();
                cmd = "%here";
            } else
                cmd = "%heredoc";
        else if (c == '=') {
            numeric = FALSE;
            return CALL;
        } else
            cmd = "%open";
        goto redirection;
    case '>':
        fd[0] = 1;
        if ((c = GETC()) == '>')
            if ((c = GETC()) == '<') {
                c = GETC();
                cmd = "%open-append";
            } else
                cmd = "%append";
        else if (c == '<') {
            c = GETC();
            cmd = "%open-create";
        } else
            cmd = "%create";
        goto redirection;
    redirection:
        w = NW;
        if (!getfds(fd, c, fd[0], DEFAULT))
            return ERROR;
        if (fd[1] != DEFAULT) {
            y->tree = (fd[1] == CLOSED)
                    ? mkclose(fd[0])
                    : mkdup(fd[0], fd[1]);
            return DUP;
        }
        y->tree = mkredircmd(cmd, fd[0]);
        return REDIR;
    }

    default:
        assert(c != '\0');
        w = NW;
        return c; /* don't know what it is, let yacc barf on it */
    }
}

extern void inityy(void) {
    newline = FALSE;
    w = NW;
    if (bufsize > BUFMAX) {     /* return memory to the system if the buffer got too large */
        efree(tokenbuf);
        tokenbuf = NULL;
    }
    if (tokenbuf == NULL) {
        bufsize = BUFSIZE;
        tokenbuf = ealloc(bufsize);
    }
}
