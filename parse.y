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

void yyparse(void *parser, int tokentype, Token tokendat, int *statep) {
	Parse(parser, tokentype, tokendat, statep);
}

void freeparser(void *parser) {
	ParseFree(parser, efree);
}
}

%extra_argument { int *statep }

%parse_failure {
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
%default_type {Tree *}

%type keyword	{char *}
%type binder	{NodeKind}

es	::= line(A) end.	{ parsetree = A; *statep = PARSE_ACCEPT; }
es	::= error end.		{ parsetree = NULL; *statep = PARSE_ERROR; }

end	::= NL.			{ if (!readheredocs(FALSE)) *statep = PARSE_ERROR; }
end	::= ENDFILE.		{ if (!readheredocs(TRUE)) *statep = PARSE_ERROR; }

line(A)	::= cmd(B).		{ A = B; *statep = PARSE_ENDLINE; }
line(A)	::= cmdsa(B) line(C).	{ A = mkseq("%seq", B, C); *statep = PARSE_ENDLINE; }

body(A)	::= cmd(B).		{ A = B; }
body(A)	::= cmdsan(B) body(C).	{ A = mkseq("%seq", B, C); }

cmdsa(A)	::= cmd(B) SCOLON.	{ A = B; }
cmdsa(A)	::= cmd(B) AMP.		{ A = prefix("%background", mk(nList, thunkify(B), NULL)); }

cmdsan(A)	::= cmdsa(B).		{ A = B; }
cmdsan(A)	::= cmd(B) NL.		{ A = B; if (!readheredocs(FALSE)) *statep = PARSE_ERROR; }

cmd(A)	::= .		[LET]			{ A = NULL; }
cmd(A)	::= simple(B).				{ A = redirect(B); if (A == &errornode) *statep = PARSE_ERROR; }
cmd(A)	::= redir(B) cmd(C).	[BANG]		{ A = redirect(mk(nRedir, B, C)); if (A == &errornode) *statep = PARSE_ERROR; }
cmd(A)	::= first(B) assign(C).			{ A = mk(nAssign, B, C); }
cmd(A)	::= fn(B).				{ A = B; }
cmd(A)	::= binder(B) nl LPAREN bindings(C) RPAREN nl cmd(D).	{ A = mk(B, C, D); }
cmd(A)	::= cmd(B) ANDAND nl cmd(C).		{ A = mkseq("%and", B, C); }
cmd(A)	::= cmd(B) OROR nl cmd(C).		{ A = mkseq("%or", B, C); }
cmd(A)	::= cmd(B) PIPE(C) nl cmd(D).		{ A = mkpipe(B, C.tree->u[0].i, C.tree->u[1].i, D); }
cmd(A)	::= BANG caret cmd(B).			{ A = prefix("%not", mk(nList, thunkify(B), NULL)); }
cmd(A)	::= TILDE word(B) words(C).		{ A = mk(nMatch, B, C); }
cmd(A)	::= EXTRACT word(B) words(C).		{ A = mk(nExtract, B, C); }
cmd(A)	::= MATCH word(B) nl LPAREN cases(C) RPAREN.	{ A = mkmatch(B, C); }

cases(A)	::= case(B).			{ A = treecons2(B, NULL); }
cases(A)	::= cases(B) SCOLON case(C).	{ A = treeconsend2(B, C); }
cases(A)	::= cases(B) NL case(C).	{ A = treeconsend2(B, C); }

case(A)	::=.				{ A = NULL; }
case(A)	::= word(B) first(C).		{ A = mk(nList, B, thunkify(C)); }

simple(A)	::= first(B).		{ A = treecons2(B, NULL); }
simple(A)	::= first(B) args(C).	{ A = firstprepend(B, C); }

args(A)	::= word(B).			{ A = treecons2(B, NULL); }
args(A)	::= redir(B).			{ A = redirappend(NULL, B); }
args(A)	::= args(B) word(C).		{ A = treeconsend2(B, C); }
args(A)	::= args(B) redir(C).		{ A = redirappend(B, C); }

redir(A)	::= DUP(B).		{ A = B.tree; }
redir(A)	::= REDIR(B) word(C).	{ A = mkredir(B.tree, C); }

bindings(A)	::= binding(B).				{ A = treecons2(B, NULL); }
bindings(A)	::= bindings(B) SCOLON binding(C).	{ A = treeconsend2(B, C); }
bindings(A)	::= bindings(B) NL binding(C).		{ A = treeconsend2(B, C); }

binding(A)	::=.			{ A = NULL; }
binding(A)	::= fn(B).		{ A = B; }
binding(A)	::= first(B) assign(C).	{ A = mk(nAssign, B, C); }

assign(A)	::= caret EQ caret words(B).	{ A = B; }

fn(A)	::= FN word(B) params(C) LBRACE body(D) RBRACE.	{ A = fnassign(B, mklambda(C, D)); }
fn(A)	::= FN word(B).					{ A = fnassign(B, NULL); }

first(A)	::= comword(B).			{ A = B; }
first(A)	::= first(B) CARET sword(C).	{ A = mk(nConcat, B, C); }

sword(A)	::= comword(B).		{ A = B; }
sword(A)	::= keyword(B).		{ A = mk(nWord, B); }

word(A)	::= sword(B).			{ A = B; }
word(A)	::= word(B) CARET sword(C).	{ A = mk(nConcat, B, C); }

comword(A)	::= param(B).				{ A = B; }
comword(A)	::= LPAREN nlwords(B) RPAREN.		{ A = B; }
comword(A)	::= LBRACE body(B) RBRACE.		{ A = thunkify(B); }
comword(A)	::= AT params(B) LBRACE body(C) RBRACE.	{ A = mklambda(B, C); }
comword(A)	::= DOLLAR sword(B).			{ A = mk(nVar, B); }
comword(A)	::= DOLLAR sword(B) SUB words(C) RPAREN.	{ A = mk(nVarsub, B, C); }
comword(A)	::= CALL sword(B).			{ A = mk(nCall, B); }
comword(A)	::= COUNT sword(B).			{ A = mk(nCall, prefix("%count", treecons(mk(nVar, B), NULL))); }
comword(A)	::= FLAT sword(B).			{ A = flatten(mk(nVar, B), " "); }
comword(A)	::= PRIM WORD(B).			{ A = mk(nPrim, B.str); }
comword(A)	::= BACKTICK sword(B).			{ A = backquote(mk(nVar, mk(nWord, "ifs")), B); }
comword(A)	::= BFLAT sword(B).			{ A = flatten(backquote(mk(nVar, mk(nWord, "ifs")), B), " "); }
comword(A)	::= BACKBACK word(B) sword(C).		{ A = backquote(B, C); }
comword(A)	::= BBFLAT word(B) sword(C).		{ A = flatten(backquote(B, C), " "); }

param(A)	::= WORD(B).		{ A = mk(nWord, B.str); }
param(A)	::= QWORD(B).		{ A = mk(nQword, B.str); }

params(A)	::= .			{ A = NULL; }
params(A)	::= params(B) param(C).	{ A = treeconsend(B, C); }

words(A)	::= .			{ A = NULL; }
words(A)	::= words(B) word(C).	{ A = treeconsend(B, C); }

nlwords(A)	::= .			{ A = NULL; }
nlwords(A)	::= nlwords(B) word(C).	{ A = treeconsend(B, C); }
nlwords(A)	::= nlwords(B) NL.	{ A = B; }

nl	::= .
nl	::= nl NL.

caret 	::= .	[CARET]
caret	::= CARET.

binder(A)	::= LOCAL.	{ A = nLocal; }
binder(A)	::= LET.	{ A = nLet; }
binder(A)	::= FOR.	{ A = nFor; }
binder(A)	::= CLOSURE.	{ A = nClosure; }

keyword(A)	::= BANG.	{ A = "!"; }
keyword(A)	::= TILDE.	{ A = "~"; }
keyword(A)	::= EQ.		{ A = "="; }
keyword(A)	::= EXTRACT.	{ A = "~~"; }
keyword(A)	::= LOCAL. 	{ A = "local"; }
keyword(A)	::= LET.	{ A = "let"; }
keyword(A)	::= FOR.	{ A = "for"; }
keyword(A)	::= FN.		{ A = "fn"; }
keyword(A)	::= CLOSURE.	{ A = "%closure"; }
keyword(A)	::= MATCH.	{ A = "match"; }
