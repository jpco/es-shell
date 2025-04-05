/* dump.c -- dump es's internal state as a c program ($Revision: 1.1.1.1 $) */

#include "es.h"
#include "var.h"
#include "term.h"

/*
 * the $&dumpstate prints the appropriate C data structures for
 * representing the parts of es's memory that can be stored in
 * the text (read-only) segment of the program.  (some liberties
 * are taken with regard to what the initial.es routines can do
 * regarding changing lexically bound values in order that more
 * things can be here.)
 *
 * since these things are read-only they cannot point to structures
 * that need to be garbage collected.  (think of this like a very
 * old generation in a generational collector.)
 *
 * in order that addresses are internally consistent, garbage collection
 * is disabled during the dumping process.
 */

/* new stuff */

/* char *s are handled differently frome everything else due to how they are */

#define BASE_INDEX	((void *)0x1327)
#define CHAR_INDEX(value)	(dictget(char_dict, ((char *)(value))) - BASE_INDEX)

static int char_idx = 0;
static Dict *char_dict;

/* everything else is in a dynamic array. */

#define MIN_CAP	4

static int cap_for_size(int size) {
	int cap = MIN_CAP;
	while (cap < size)
		cap *= 2;
	return cap;
}

static int scanfor(void *obj, void **arr, int len) {
	int i;
	for (i = 0; i < len; i++)
		if (arr[i] == obj)
			return i;
	return -1;
}

static int tree_s_size	= 0;
static int tree_p_size	= 0;
static int tree_pp_size	= 0;
static int binding_size	= 0;
static int closure_size	= 0;
static int term_size	= 0;
static int list_size	= 0;

static Tree	**tree_s_arr;
static Tree	**tree_p_arr;
static Tree	**tree_pp_arr;
static Binding	**binding_arr;
static Closure	**closure_arr;
static Term	**term_arr;
static List	**list_arr;

/* the "account" functions put everything in nice sorted data structures so
 * they can refer to each other later */

static void account_chars	(char *);
static void account_tree_s	(Tree *);
static void account_tree_p	(Tree *);
static void account_tree_pp	(Tree *);
static void account_tree	(Tree *);
static void account_binding	(Binding *);
static void account_closure	(Closure *);
static void account_term	(Term *);
static void account_list	(List *);

static void account_chars(char *str) {
	if (str == NULL || dictget(char_dict, str) != NULL)
		return;
	char_dict = dictput(char_dict, str, BASE_INDEX + char_idx);
	char_idx += strlen(str) + 1;
}

static void **account_ptr(void *obj, void **arr, int size) {
	if (scanfor(obj, arr, size) != -1)
		return NULL;
	arr[size++] = obj;
	if (size == cap_for_size(size))
		arr = erealloc(arr, sizeof(void *) * size * 2);
	return arr;
}

static char *tree_arr_str(Tree *t) {
	switch (t->kind) {
	default:
		panic("dumptree: bad node kind %d", t->kind);
	case nWord: case nQword: case nPrim:
		return "trees_s";
	case nCall: case nThunk: case nVar:
		return "trees_p";
	case nAssign: case nConcat: case nClosure: case nFor: case nLambda:
	case nLet: case nList: case nLocal: case nVarsub: case nMatch:
	case nExtract:
		return "trees_pp";
	}
}

static int scanfortree(Tree *t);

static int scanfortree_s(Tree *t) {
	int i;
	for (i = 0; i < tree_s_size; i++) {
		if (tree_s_arr[i]->kind != t->kind)
			continue;
		if (!streq(tree_s_arr[i]->u[0].s, t->u[0].s))
			continue;
		return i;
	}
	return -1;
}

/* NOTE: requires t1 and t2 to have been account_tree'd already */
static Boolean childtreeq(char *a1, int i1, Tree *t2) {
	if (a1 == NULL && t2 == NULL)
		return TRUE;
	if (a1 == NULL || t2 == NULL)
		return FALSE;
	if (a1 != tree_arr_str(t2))
		return FALSE;
	return (i1 == scanfortree(t2));
}

static int scanfortree_p(Tree *t) {
	int i;
	char *a1 = (t->u[0].p == NULL ? NULL : tree_arr_str(t->u[0].p));
	int i1 = (t->u[0].p == NULL ? -1 : scanfortree(t->u[0].p));
	for (i = 0; i < tree_p_size; i++)
		if (t->kind == tree_p_arr[i]->kind
				&& childtreeq(a1, i1, tree_p_arr[i]->u[0].p))
			return i;
	return -1;
}

static int scanfortree_pp(Tree *t) {
	int i;
	char *a1 = (t->u[0].p == NULL ? NULL : tree_arr_str(t->u[0].p));
	int i1 = (t->u[0].p == NULL ? -1 : scanfortree(t->u[0].p));
	char *a2 = (t->u[1].p == NULL ? NULL : tree_arr_str(t->u[1].p));
	int i2 = (t->u[1].p == NULL ? -1 : scanfortree(t->u[1].p));
	for (i = 0; i < tree_pp_size; i++)
		if (t->kind == tree_pp_arr[i]->kind
				&& childtreeq(a1, i1, tree_pp_arr[i]->u[0].p)
				&& childtreeq(a2, i2, tree_pp_arr[i]->u[1].p))
			return i;
	return -1;
}

static int scanfortree(Tree *t) {
	int i;

	/* memoize as this function otherwise blows up exponentially */
	static struct tree_idx { Tree *t; int i; } *tree_idxes = NULL;
	static int tree_idx_size = 0;

	if (tree_idxes == NULL)
		tree_idxes = ealloc(sizeof(struct tree_idx) * MIN_CAP);
	for (i = 0; i < tree_idx_size; i++)
		if (tree_idxes[i].t == t)
			return tree_idxes[i].i;

	switch (t->kind) {
	default:
		panic("dumptree: bad node kind %d", t->kind);
	case nWord: case nQword: case nPrim:
		i = scanfortree_s(t);
		break;
	case nCall: case nThunk: case nVar:
		i = scanfortree_p(t);
		break;
	case nAssign: case nConcat: case nClosure: case nFor: case nLambda:
	case nLet: case nList: case nLocal: case nVarsub: case nMatch:
	case nExtract:
		i = scanfortree_pp(t);
		break;
	}

	if (i == -1)
		return i;

	tree_idxes[tree_idx_size].t = t;
	tree_idxes[tree_idx_size++].i = i;
	if (tree_idx_size == cap_for_size(tree_idx_size))
		tree_idxes = erealloc(tree_idxes,
				sizeof(struct tree_idx) * tree_idx_size * 2);
	return i;
}

static void account_tree_s(Tree *t) {
	account_chars(t->u[0].s);
	if (scanfortree_s(t) != -1)
		return;

	tree_s_arr[tree_s_size++] = t;
	if (tree_s_size == cap_for_size(tree_s_size))
		tree_s_arr = erealloc(tree_s_arr, sizeof(void *) * tree_s_size * 2);
}

static void account_tree_p(Tree *t) {
	account_tree(t->u[0].p);
	if (scanfortree_p(t) != -1)
		return;

	tree_p_arr[tree_p_size++] = t;
	if (tree_p_size == cap_for_size(tree_p_size))
		tree_p_arr = erealloc(tree_p_arr, sizeof(void *) * tree_p_size * 2);
}

static void account_tree_pp(Tree *t) {
	account_tree(t->u[0].p);
	account_tree(t->u[1].p);
	if (scanfortree_pp(t) != -1)
		return;

	tree_pp_arr[tree_pp_size++] = t;
	if (tree_pp_size == cap_for_size(tree_pp_size))
		tree_pp_arr = erealloc(tree_pp_arr, sizeof(void *) * tree_pp_size * 2);
}

static void account_tree(Tree *t) {
	if (t == NULL)
		return;
	switch (t->kind) {
	default:
		panic("dumptree: bad node kind %d", t->kind);
	case nWord: case nQword: case nPrim:
		account_tree_s(t);
		break;
	case nCall: case nThunk: case nVar:
		account_tree_p(t);
		break;
	case nAssign: case nConcat: case nClosure: case nFor: case nLambda:
	case nLet: case nList: case nLocal: case nVarsub: case nMatch:
	case nExtract:
		account_tree_pp(t);
		break;
	}
}

static void account_binding(Binding *b) {
	Binding **narr;
	if (b == NULL)
		return;
	account_chars(b->name);
	account_list(b->defn);
	account_binding(b->next);

	narr = (Binding **) account_ptr((void *)b, (void **)binding_arr, binding_size);
	if (narr != NULL) {
		binding_arr = narr;
		binding_size++;
	}
}

static int scanforclosure(Closure *c) {
	int i;
	int bi = scanfor(c->binding, (void **)binding_arr, binding_size);
	char *ta = tree_arr_str(c->tree);
	int ti = scanfortree(c->tree);
	for (i = 0; i < closure_size; i++) {
		if (bi != scanfor(closure_arr[i]->binding, (void **)binding_arr, binding_size))
			continue;
		if (ta != tree_arr_str(closure_arr[i]->tree))
			continue;
		if (ti != scanfortree(closure_arr[i]->tree))
			continue;
		return i;
	}
	return -1;
}

static void account_closure(Closure *c) {
	if (c == NULL)
		return;
	account_binding(c->binding);
	account_tree(c->tree);

	if (scanforclosure(c) != -1)
		return;

	closure_arr[closure_size++] = c;
	if (closure_size == cap_for_size(closure_size))
		closure_arr = erealloc(closure_arr, sizeof(void *) * closure_size * 2);
}

static int scanforterm(Term *t) {
	int i;
	int ci = (t->closure == NULL ? -1 : scanforclosure(t->closure));
	for (i = 0; i < term_size; i++) {
		if (t->closure != NULL) {
			if (term_arr[i]->closure == NULL)
				continue;
			if (scanforclosure(term_arr[i]->closure) == ci)
				return i;
		}
		if (t->str != NULL) {
			if (term_arr[i]->str == NULL)
				continue;
			if (streq(term_arr[i]->str, t->str))
				return i;
		}
	}
	return -1;
}

static void account_term(Term *t) {
	if (t == NULL || scanforterm(t) != -1)
		return;
	account_chars(t->str);
	account_closure(t->closure);

	term_arr[term_size++] = t;
	if (term_size == cap_for_size(term_size))
		term_arr = erealloc(term_arr, sizeof(Term *) * term_size * 2);
}

static void account_list(List *l) {
	List **narr;
	if (l == NULL)
		return;
	account_term(l->term);
	account_list(l->next);

	narr = (List **) account_ptr((void *)l, (void **)list_arr, list_size);
	if (narr != NULL) {
		list_arr = narr;
		list_size++;
	}
}

static void account(void UNUSED *ignore, char *key, void *value) {
	Var *var = value;
	account_chars(key);
	account_list(var->defn);
}

/* print 'em */

static const char *nodename(NodeKind k) {
	switch(k) {
	default:	panic("nodename: bad node kind %d", k);
	case nAssign:	return "Assign";
	case nCall:	return "Call";
	case nClosure:	return "Closure";
	case nConcat:	return "Concat";
	case nFor:	return "For";
	case nLambda:	return "Lambda";
	case nLet:	return "Let";
	case nList:	return "List";
	case nLocal:	return "Local";
	case nMatch:	return "Match";
	case nExtract:	return "Extract";
	case nPrim:	return "Prim";
	case nQword:	return "Qword";
	case nThunk:	return "Thunk";
	case nVar:	return "Var";
	case nVarsub:	return "Varsub";
	case nWord:	return "Word";
	}
}

/* this is the weird case. */
static void slab_chars(void *slabp, char *key, void *val) {
	char *slab = (char *) slabp;
	int idx = (int) (val - BASE_INDEX);
	int len = strlen(key);
	memcpy(slab + idx, key, len);
	slab[(size_t)(idx + len)] = '\0';
}

static void print_chars(void) {
	char *slab;
	int i;
	if (char_idx == 0)
		return;

	slab = ealloc(sizeof(char) * char_idx);
	dictforall(char_dict, slab_chars, slab);

	print("static const char chars[%d] = {\n\t/* 0 */ ", char_idx);
	for (i = 0; i < char_idx; i++) {
		char s = slab[i];
		if (s == 0)
			print(" 0,\n\t/* %d */", i+1);
		else if (isprint(s))
			print(" '%c',", s);
		else
			print(" %d,", s);
	}
	print("\n};\n\n");
	efree(slab);
}

static void print_trees_s(void) {
	int i;
	if (tree_s_size == 0)
		return;
	print("static const Tree_s trees_s[%d] = {\n", tree_s_size);
	for (i = 0; i < tree_s_size; i++) {
		print("\t{ n%s, { { (char *) &chars[%d] } } },\n",
				nodename(tree_s_arr[i]->kind),
				CHAR_INDEX(tree_s_arr[i]->u[0].s));
	}
	print("};\n\n");
}

static void print_trees_p(void) {
	int i;
	if (tree_p_size == 0)
		return;
	print("static const Tree_p trees_p[%d] = {\n", tree_p_size);
	for (i = 0; i < tree_p_size; i++) {
		Tree *t = tree_p_arr[i]->u[0].p;
		print("\t{ n%s, { { ", nodename(tree_p_arr[i]->kind));
		if (t == NULL)
			print("NULL");
		else
			print("(Tree *) &%s[%d]", tree_arr_str(t), scanfortree(t));
		print(" } } },\n");
	}
	print("};\n\n");
}

static void print_trees_pp(void) {
	int i;
	if (tree_pp_size == 0)
		return;
	print("static const Tree_pp trees_pp[%d] = {\n", tree_pp_size);
	for (i = 0; i < tree_pp_size; i++) {
		Tree *t0 = tree_pp_arr[i]->u[0].p;
		Tree *t1 = tree_pp_arr[i]->u[1].p;
		print("\t{ n%s, { { ", nodename(tree_pp_arr[i]->kind));
		if (t0 == NULL)
			print("NULL");
		else
			print("(Tree *) &%s[%d]", tree_arr_str(t0), scanfortree(t0));
		print(" }, { ");
		if (t1 == NULL)
			print("NULL");
		else
			print("(Tree *) &%s[%d]", tree_arr_str(t1), scanfortree(t1));
		print(" } } },\n");
	}
	print("};\n\n");
}

static void print_bindings(void) {
	int i;
	if (binding_size == 0)
		return;
	print("static const Binding bindings[%d] = {\n", binding_size);
	for (i = 0; i < binding_size; i++) {
		char *name = binding_arr[i]->name;
		List *defn = binding_arr[i]->defn;
		Binding *next = binding_arr[i]->next;

		print("\t{ ");
		if (name == NULL)
			print("NULL");
		else
			print("(char *) &chars[%d]", CHAR_INDEX(name));
		print(", ");
		if (defn == NULL)
			print("NULL");
		else
			print("(List *) &lists[%d]", scanfor(defn, (void **) list_arr, list_size));
		print(", ");
		if (next == NULL)
			print("NULL");
		else
			print("(Binding *) &bindings[%d]", scanfor(next, (void **) binding_arr, binding_size));
		print(" },\n");
	}
	print("};\n\n");
}

static void print_closures(void) {
	int i;
	if (closure_size == 0)
		return;
	print("static const Closure closures[%d] = {\n", closure_size);
	for (i = 0; i < closure_size; i++) {
		Binding *b = closure_arr[i]->binding;
		Tree *t = closure_arr[i]->tree;

		print("\t{ ");
		if (b == NULL)
			print("NULL");
		else
			print("(Binding *) &bindings[%d]", scanfor(b, (void **) binding_arr, binding_size));
		print(", ");
		if (t == NULL)
			print("NULL");
		else
			print("(Tree *) &%s[%d]", tree_arr_str(t), scanfortree(t));
		print(" },\n");
	}
	print("};\n\n");
}

static void print_terms(void) {
	int i;
	if (term_size == 0)
		return;
	print("static const Term terms[%d] = {\n", term_size);
	for (i = 0; i < term_size; i++) {
		char *str = term_arr[i]->str;
		Closure *closure = term_arr[i]->closure;

		print("\t{ ");
		if (str == NULL)
			print("NULL");
		else
			print("(char *) &chars[%d]", CHAR_INDEX(str));
		print(", ");
		if (closure == NULL)
			print("NULL");
		else
			print("(Closure *) &closures[%d]", scanforclosure(closure));
		print(" },\n");
	}
	print("};\n\n");
}

static void print_lists(void) {
	int i;
	if (list_size == 0)
		return;
	print("static const List lists[%d] = {\n", list_size);
	for (i = 0; i < list_size; i++) {
		Term *term = list_arr[i]->term;
		List *next = list_arr[i]->next;

		print("\t{ ");
		if (term == NULL)
			print("NULL");
		else
			print("(Term *) &terms[%d]", scanforterm(term));
		print(", ");
		if (next == NULL)
			print("NULL");
		else
			print("(List *) &lists[%d]", scanfor(next, (void **) list_arr, list_size));
		print(" },\n");
	}
	print("};\n\n");
}

/* old stuff */

static void dumpdef(char *name, Var *var) {
	print("\t{ &chars[%d]\t, &lists[%d] },\n",
			CHAR_INDEX(name),
			scanfor(var->defn, (void **)list_arr, list_size));
}

static void dumpfunctions(void UNUSED *ignore, char *key, void *value) {
	if (hasprefix(key, "fn-"))
		dumpdef(key, value);
}

static void dumpsettors(void UNUSED *ignore, char *key, void *value) {
	if (hasprefix(key, "set-"))
		dumpdef(key, value);
}

static void dumpvariables(void UNUSED *ignore, char *key, void *value) {
	if (!hasprefix(key, "fn-") && !hasprefix(key, "set-"))
		dumpdef(key, value);
}

static void printheader(List *title) {
	print("/* %L */\n\n#include \"es.h\"\n#include \"term.h\"\n\n", title, " ");
	print("typedef struct { NodeKind k; struct { char *s; } u[1]; } Tree_s;\n");
	print("typedef struct { NodeKind k; struct { Tree *p; } u[1]; } Tree_p;\n");
	print("typedef struct { NodeKind k; struct { Tree *p; } u[2]; } Tree_pp;\n\n");

	if (char_idx > 0)
		print("static const char\tchars[%d];\n", char_idx);
	if (tree_s_size > 0)
		print("static const Tree_s\ttrees_s[%d];\n", tree_s_size);
	if (tree_p_size > 0)
		print("static const Tree_p\ttrees_p[%d];\n", tree_p_size);
	if (tree_pp_size > 0)
		print("static const Tree_pp\ttrees_pp[%d];\n", tree_pp_size);
	if (binding_size > 0)
		print("static const Binding\tbindings[%d];\n", binding_size);
	if (closure_size > 0)
		print("static const Closure\tclosures[%d];\n", closure_size);
	if (term_size > 0)
		print("static const Term\tterms[%d];\n", term_size);
	if (list_size > 0)
		print("static const List\tlists[%d];\n", list_size);

	print("\n");
}

extern void runinitial(void) {
	List *title = runfd(0, "initial.es", 0);

	gcdisable();

	char_dict	= mkdict();
	tree_s_arr	= ealloc(sizeof(void *) * MIN_CAP);
	tree_p_arr	= ealloc(sizeof(void *) * MIN_CAP);
	tree_pp_arr	= ealloc(sizeof(void *) * MIN_CAP);
	binding_arr	= ealloc(sizeof(void *) * MIN_CAP);
	closure_arr	= ealloc(sizeof(void *) * MIN_CAP);
	term_arr	= ealloc(sizeof(void *) * MIN_CAP);
	list_arr	= ealloc(sizeof(void *) * MIN_CAP);

	dictforall(vars, account, NULL);

	printheader(title);

	print_chars();
	print_trees_s();
	print_trees_p();
	print_trees_pp();
	print_bindings();
	print_closures();
	print_terms();
	print_lists();

	/* these must be assigned in this order, or things just won't work */
	print("static const struct { const char *name; const List *value; } defs[] = {\n");
	dictforall(vars, dumpfunctions, NULL);
	dictforall(vars, dumpsettors, NULL);
	dictforall(vars, dumpvariables, NULL);
	print("\t{ NULL, NULL }\n");
	print("};\n\n");

	print("extern void runinitial(void) {\n");
	print("\tint i;\n");
	print("\tfor (i = 0; defs[i].name != NULL; i++)\n");
	print("\t\tvardef((char *) defs[i].name, NULL, (List *) defs[i].value);\n");
	print("}\n");

	exit(0);
}
