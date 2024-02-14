/* input.h -- definitions for es lexical analyzer ($Revision: 1.1.1.1 $) */

#define	MAXUNGET	2		/* maximum 2 character pushback */

typedef struct Input Input;
struct Input {
	int (*get)(Input *self);
	int (*fill)(Input *self), (*rfill)(Input *self);
	void (*cleanup)(Input *self);
	Input *prev;
	const char *name;
	unsigned char *buf, *bufend, *bufbegin, *rbuf;
	size_t buflen;
	int unget[MAXUNGET];
	int ungot;
	int lineno;
	int fd;
	int runflags;
};


#define	GETC()		(*input->get)(input)
#define	UNGETC(c)	unget(input, c)


/* input.c */

extern Input *input;
extern void unget(Input *in, int c);
extern Boolean disablehistory;
extern void yyerror(char *s);


/* token.c */

typedef enum { TK_NONE, TK_STR, TK_TREE, TK_NODEKIND } TokenType;
typedef struct {
	TokenType type;
	union {
		char *str;
		Tree *tree;
		NodeKind nk;
	} u;
} Token;

extern const char dnw[];
extern int yylex(Token *y);
extern void inityy(void);
extern void print_prompt2(void);


/* parse.y */

typedef struct {
	Tree *parsetree;
	int state;
} ParseState;

typedef struct yyParser Parser;

extern void initparse(void);
extern Parser *mkparser(void);
extern void yyparse(Parser *, int, Token *, ParseState *);
extern void freeparser(Parser *);

#define	PARSE_CONTINUE		0
#define	PARSE_ACCEPT		1
#define	PARSE_ERROR		2
#define	PARSE_HEREDOC_ACCEPT	3
#define	PARSE_HEREDOC_ENDFILE	4
#define	PARSE_HEREDOC_CONTINUE	5

/* heredoc.c */

extern void emptyherequeue(void);
