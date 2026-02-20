/* input.h -- definitions for es lexical analyzer ($Revision: 1.1.1.1 $) */

#define	MAXUNGET	2		/* maximum 2 character pushback */

#include "token.h"	/* for YYSTYPE */

/* Input contains state that lasts longer than a $&parse. */
struct Input {
	/* previous Input in the stack */
	Input *prev;

	/* functions used to pull from Input */
	int (*get)(Input *self);
	int (*fill)(Input *self);
	void (*cleanup)(Input *self);

	/* input buffer variables */
	size_t buflen;
	unsigned char *buf, *bufend, *bufbegin, *rbuf;

	/* parser pushback buffer */
	int unget[MAXUNGET];
	int ungot;

	/* input metadata and flags */
	const char *name;
	int lineno;
	int fd;
	int runflags;
};

/* Parser contains state that lasts for one call to $&parse or less. */
struct Parser {
	Input *input;
};


/* input.c */

extern int get(Parser *p);
extern void unget(Parser *p, int c);
extern Boolean ignoreeof;
extern void yyerror(Parser *p, const char *s);


/* token.c */

extern const char dnw[];
extern int yylex(YYSTYPE *y, Parser *p);
extern void inityy(void);
extern void print_prompt2(Parser *p);


/* parse.y */

extern Tree *parsetree;
extern int yyparse(Parser *p);


/* heredoc.c */

extern void emptyherequeue(void);
