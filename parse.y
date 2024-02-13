/* lemon.y -- grammar for es ($Revision: 1.2 $) */

/* Which of these includes are strictly necessary? */
%include{
#include "es.h"
#include "input.h"
#include "syntax.h"
}

%code{
Boolean parsetrace = FALSE;

void *mkparser() {
	void *p = ParseAlloc(ealloc);
	if (parsetrace)
		ParseTrace(stdout, "	> ");
	return p;
}

/* A big ol' hack.  Forces the parser to do a reduce if that's the pending
 * action.  This should REALLY be implemented as part of Parse() in lempar.c. */
static void yyreducepending(void *pp) {
	yyParser *parser = (yyParser*)pp;
	YYACTIONTYPE yyact = parser->yytos->stateno;
	int dobreak = 0;
	Token ignored;
	while (yyact >= YY_MIN_REDUCE) {
		yyact = yy_reduce_pending(parser, yyact, ignored, &dobreak);
		if (dobreak)
			break;
	}
}

void yyparse(void *parser, int tokentype, Token tokendat, int *statep) {
	Parse(parser, tokentype, tokendat, statep);
	yyreducepending(parser);
}

void freeparser(void *parser) {
	ParseFree(parser, efree);
}

}

%extra_argument { int *statep }

%syntax_error {
	yyerror("syntax error");
	*statep = PARSE_ERROR;
}

/* uncomment if any of these need precedence declared?
%nonassoc	WORD QWORD.
%nonassoc	LOCAL LET FOR CLOSURE FN.
%nonassoc	ANDAND BACKBACK BBFLAT BFLAT EXTRACT CALL COUNT DUP FLAT OROR PRIM REDIR SUB.
%nonassoc	NL ENDFILE ERROR.
%nonassoc	MATCH.
*/

%token	ERROR.

%left	CARET EQ.
%left	MATCH LOCAL LET FOR CLOSURE RPAREN.
%left	ANDAND OROR NL.
%left	BANG.
%left	PIPE.
%right	DOLLAR.
%left	SUB.

/* are these new or something?
%realloc erealloc
%free	 efree */

/* WORD QWORD: char *. */
/* REDIR PIPE DUP: Tree *. */
/* Everything else: Nothing. */
%token_type {Token}
%default_type {Token}

%include{
Token tk(Tree *t) {
	Token tk;
	tk.type = TK_TREE;
	tk.u.tree = t;
	return tk;
}

Token tkstr(char *str) {
	Token tk;
	tk.type = TK_STR;
	tk.u.str = str;
	return tk;
}

Token tknk(NodeKind nk) {
	Token tk;
	tk.type = TK_NODEKIND;
	tk.u.nk = nk;
	return tk;
}

#define T(n) (n).u.tree
}

/*
%type keyword	{char *}
%type binder	{NodeKind}
*/

main	::= es end.

es	::= line(A).		{ parsetree = T(A);	*statep = PARSE_ACCEPT; }
es	::= error.		{ parsetree = NULL;	*statep = PARSE_ERROR; }

end	::= NL.			{ *statep = PARSE_HEREDOC_ACCEPT; }
end	::= ENDFILE.		{ *statep = PARSE_HEREDOC_ENDFILE; }

line(A)	::= cmd(B).		{ A = B; }
line(A)	::= cmdsa(B) line(C).	{ A = tk(mkseq("%seq", T(B), T(C))); }

body(A)	::= cmd(B).		{ A = B; }
body(A)	::= cmdsan(B) body(C).	{ A = tk(mkseq("%seq", T(B), T(C))); }

cmdsa(A)	::= cmd(B) SCOLON.	{ A = B; }
cmdsa(A)	::= cmd(B) AMP.		{ A = tk(prefix("%background", mk(nList, thunkify(T(B)), NULL))); }

cmdsan(A)	::= cmdsa(B).		{ A = B; }
cmdsan(A)	::= cmd(B) NL.		{ A = B; *statep = PARSE_HEREDOC_CONTINUE; }

cmd(A)	::= .		[LET]			{ A = tk(NULL); }
cmd(A)	::= simple(B).				{ A = tk(redirect(T(B))); if (T(A) == &errornode) *statep = PARSE_ERROR; }
cmd(A)	::= redir(B) cmd(C).	[BANG]		{ A = tk(redirect(mk(nRedir, T(B), T(C)))); if (T(A) == &errornode) *statep = PARSE_ERROR; }
cmd(A)	::= first(B) assign(C).			{ A = tk(mk(nAssign, T(B), T(C))); }
cmd(A)	::= fn(B).				{ A = B; }
cmd(A)	::= binder(B) nl LPAREN bindings(C) RPAREN nl cmd(D).	{ A = tk(mk(B.u.nk, T(C), T(D))); }
cmd(A)	::= cmd(B) ANDAND nl cmd(C).		{ A = tk(mkseq("%and", T(B), T(C))); }
cmd(A)	::= cmd(B) OROR nl cmd(C).		{ A = tk(mkseq("%or", T(B), T(C))); }
cmd(A)	::= cmd(B) PIPE(C) nl cmd(D).		{ A = tk(mkpipe(T(B), T(C)->u[0].i, T(C)->u[1].i, T(D))); }
cmd(A)	::= BANG caret cmd(B).			{ A = tk(prefix("%not", mk(nList, thunkify(T(B)), NULL))); }
cmd(A)	::= TILDE word(B) words(C).		{ A = tk(mk(nMatch, T(B), T(C))); }
cmd(A)	::= EXTRACT word(B) words(C).		{ A = tk(mk(nExtract, T(B), T(C))); }
cmd(A)	::= MATCH word(B) nl LPAREN cases(C) RPAREN.	{ A = tk(mkmatch(T(B), T(C))); }

cases(A)	::= case(B).			{ A = tk(treecons2(T(B), NULL)); }
cases(A)	::= cases(B) SCOLON case(C).	{ A = tk(treeconsend2(T(B), T(C))); }
cases(A)	::= cases(B) NL case(C).	{ A = tk(treeconsend2(T(B), T(C))); }

case(A)	::=.				{ A = tk(NULL); }
case(A)	::= word(B) first(C).		{ A = tk(mk(nList, T(B), thunkify(T(C)))); }

simple(A)	::= first(B).		{ A = tk(treecons2(T(B), NULL)); }
simple(A)	::= first(B) args(C).	{ A = tk(firstprepend(T(B), T(C))); }

args(A)	::= word(B).			{ A = tk(treecons2(T(B), NULL)); }
args(A)	::= redir(B).			{ A = tk(redirappend(NULL, T(B))); }
args(A)	::= args(B) word(C).		{ A = tk(treeconsend2(T(B), T(C))); }
args(A)	::= args(B) redir(C).		{ A = tk(redirappend(T(B), T(C))); }

redir(A)	::= DUP(B).		{ A = B; }
redir(A)	::= REDIR(B) word(C).	{ A = tk(mkredir(T(B), T(C))); }

bindings(A)	::= binding(B).				{ A = tk(treecons2(T(B), NULL)); }
bindings(A)	::= bindings(B) SCOLON binding(C).	{ A = tk(treeconsend2(T(B), T(C))); }
bindings(A)	::= bindings(B) NL binding(C).		{ A = tk(treeconsend2(T(B), T(C))); }

binding(A)	::=.			{ A = tk(NULL); }
binding(A)	::= fn(B).		{ A = B; }
binding(A)	::= first(B) assign(C).	{ A = tk(mk(nAssign, T(B), T(C))); }

assign(A)	::= caret EQ caret words(B).	{ A = B; }

fn(A)	::= FN word(B) params(C) LBRACE body(D) RBRACE.	{ A = tk(fnassign(T(B), mklambda(T(C), T(D)))); }
fn(A)	::= FN word(B).					{ A = tk(fnassign(T(B), NULL)); }

first(A)	::= comword(B).			{ A = B; }
first(A)	::= first(B) CARET sword(C).	{ A = tk(mk(nConcat, T(B), T(C))); }

sword(A)	::= comword(B).		{ A = B; }
sword(A)	::= keyword(B).		{ A = tk(mk(nWord, B.u.str)); }

word(A)	::= sword(B).			{ A = B; }
word(A)	::= word(B) CARET sword(C).	{ A = tk(mk(nConcat, T(B), T(C))); }

comword(A)	::= param(B).				{ A = B; }
comword(A)	::= LPAREN nlwords(B) RPAREN.		{ A = B; }
comword(A)	::= LBRACE body(B) RBRACE.		{ A = tk(thunkify(T(B))); }
comword(A)	::= AT params(B) LBRACE body(C) RBRACE.	{ A = tk(mklambda(T(B), T(C))); }
comword(A)	::= DOLLAR sword(B).			{ A = tk(mk(nVar, T(B))); }
comword(A)	::= DOLLAR sword(B) SUB words(C) RPAREN.	{ A = tk(mk(nVarsub, T(B), T(C))); }
comword(A)	::= CALL sword(B).			{ A = tk(mk(nCall, T(B))); }
comword(A)	::= COUNT sword(B).			{ A = tk(mk(nCall, prefix("%count", treecons(mk(nVar, T(B)), NULL)))); }
comword(A)	::= FLAT sword(B).			{ A = tk(flatten(mk(nVar, T(B)), " ")); }
comword(A)	::= PRIM WORD(B).			{ A = tk(mk(nPrim, B.u.str)); }
comword(A)	::= BACKTICK sword(B).			{ A = tk(backquote(mk(nVar, mk(nWord, "ifs")), T(B))); }
comword(A)	::= BFLAT sword(B).			{ A = tk(flatten(backquote(mk(nVar, mk(nWord, "ifs")), T(B)), " ")); }
comword(A)	::= BACKBACK word(B) sword(C).		{ A = tk(backquote(T(B), T(C))); }
comword(A)	::= BBFLAT word(B) sword(C).		{ A = tk(flatten(backquote(T(B), T(C)), " ")); }

param(A)	::= WORD(B).		{ A = tk(mk(nWord, B.u.str)); }
param(A)	::= QWORD(B).		{ A = tk(mk(nQword, B.u.str)); }

params(A)	::= .			{ A = tk(NULL); }
params(A)	::= params(B) param(C).	{ A = tk(treeconsend(T(B), T(C))); }

words(A)	::= .			{ A = tk(NULL); }
words(A)	::= words(B) word(C).	{ A = tk(treeconsend(T(B), T(C))); }

nlwords(A)	::= .			{ A = tk(NULL); }
nlwords(A)	::= nlwords(B) word(C).	{ A = tk(treeconsend(T(B), T(C))); }
nlwords(A)	::= nlwords(B) NL.	{ A = B; }

nl	::= .
nl	::= nl NL.

caret 	::= .	[CARET]
caret	::= CARET.

binder(A)	::= LOCAL.	{ A = tknk(nLocal); }
binder(A)	::= LET.	{ A = tknk(nLet); }
binder(A)	::= FOR.	{ A = tknk(nFor); }
binder(A)	::= CLOSURE.	{ A = tknk(nClosure); }

keyword(A)	::= BANG.	{ A = tkstr("!"); }
keyword(A)	::= TILDE.	{ A = tkstr("~"); }
keyword(A)	::= EQ.		{ A = tkstr("="); }
keyword(A)	::= EXTRACT.	{ A = tkstr("~~"); }
keyword(A)	::= LOCAL. 	{ A = tkstr("local"); }
keyword(A)	::= LET.	{ A = tkstr("let"); }
keyword(A)	::= FOR.	{ A = tkstr("for"); }
keyword(A)	::= FN.		{ A = tkstr("fn"); }
keyword(A)	::= CLOSURE.	{ A = tkstr("%closure"); }
keyword(A)	::= MATCH.	{ A = tkstr("match"); }
