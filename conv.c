/* conv.c -- convert between internal and external forms ($Revision: 1.1.1.1 $) */

#include "es.h"
#include "print.h"
#include "term.h"

/* %L -- print a list */
static Boolean Lconv(Format *f) {
    List *lp, *next;
    char *sep;
    const char *fmt = (f->flags & FMT_altform) ? "%S%s" : "%s%s";

    lp = va_arg(f->args, List *);
    sep = va_arg(f->args, char *);
    for (; lp != NULL; lp = next) {
        next = lp->next;
        fmtprint(f, fmt, getstr(lp->term), next == NULL ? "" : sep);
    }
    return FALSE;
}

/* treecount -- count the number of nodes in a flattened tree list */
static int treecount(Tree *tree) {
    return tree == NULL
         ? 0
         : tree->kind == nList
            ? treecount(tree->u[0].p) + treecount(tree->u[1].p)
            : 1;
}

/* binding -- print a binding statement */
static void binding(Format *f, char *keyword, Tree *tree) {
    Tree *np;
    char *sep = "";
    fmtprint(f, "%s (", keyword);
    for (np = tree->u[0].p; np != NULL; np = np->u[1].p) {
        Tree *binding;
        assert(np->kind == nList);
        binding = np->u[0].p;
        assert(binding != NULL);
        assert(binding->kind == nAssign);
        if (binding->u[1].p == NULL)
            fmtprint(f, "%s%#T", sep, binding->u[0].p);
        else
            fmtprint(f, "%s%#T = %T", sep, binding->u[0].p, binding->u[1].p);
        sep = "; ";
    }
    fmtprint(f, ") ");
}

/* %T -- print a tree */
static Boolean Tconv(Format *f) {
    Tree *n = va_arg(f->args, Tree *);
    Boolean group = (f->flags & FMT_altform) != 0;


#define tailcall(tree, altform) \
    STMT(n = (tree); group = (altform); goto top)

top:
    if (n == NULL) {
        if (group)
            fmtcat(f, "()");
        return FALSE;
    }

    switch (n->kind) {

    case nWord:
        fmtprint(f, "%s", n->u[0].s);
        return FALSE;

    case nQword:
        fmtprint(f, "%#S", n->u[0].s);
        return FALSE;

    case nPrim:
        fmtprint(f, "$&%s", n->u[0].s);
        return FALSE;

    case nAssign:
        fmtprint(f, "%#T = ", n->u[0].p);
        tailcall(n->u[1].p, FALSE);

    case nConcat:
        fmtprint(f, "%#T^", n->u[0].p);
        tailcall(n->u[1].p, TRUE);

    case nMatch:
        fmtprint(f, "~ %#T", n->u[0].p);
        if (treecount(n->u[1].p) > 0) {
            fmtprint(f, " ");
            tailcall(n->u[1].p, FALSE);
        } else
            return FALSE;

    case nExtract:
        fmtprint(f, "~~ %#T", n->u[0].p);
        if (treecount(n->u[1].p) > 0) {
            fmtprint(f, " ");
            tailcall(n->u[1].p, FALSE);
        } else
            return FALSE;

    case nVarsub:
        fmtprint(f, "$%#T(%T)", n->u[0].p, n->u[1].p);
        return FALSE;

    case nLocal:
        binding(f, "local", n);
        tailcall(n->u[1].p, FALSE);

    case nLet:
        binding(f, "let", n);
        tailcall(n->u[1].p, FALSE);

    case nFor:
        binding(f, "for", n);
        tailcall(n->u[1].p, FALSE);

    case nClosure:
        binding(f, "%closure", n);
        tailcall(n->u[1].p, FALSE);


    case nCall: {
        Tree *t = n->u[0].p;
        fmtprint(f, "<=");
        if (t != NULL && ((t->kind == nLambda && t->u[0].p == NULL)
                    || t->kind == nPrim))
            tailcall(t, FALSE);
        fmtprint(f, "{%T}", t);
        return FALSE;
    }

    case nVar:
        fmtputc(f, '$');
        n = n->u[0].p;
        if (n == NULL || n->kind == nWord || n->kind == nQword)
            goto top;
        fmtprint(f, "(%#T)", n);
        return FALSE;

    case nLambda:
        if (n->u[0].p)
            fmtprint(f, "@ %T ", n->u[0].p);
        fmtprint(f, "{%T}", n->u[1].p);
        return FALSE;

    case nList:
        if (!group) {
            for (; n->u[1].p != NULL; n = n->u[1].p)
                fmtprint(f, "%T ", n->u[0].p);
            n = n->u[0].p;
            goto top;
        }
        switch (treecount(n)) {
        case 0:
            fmtcat(f, "()");
            break;
        case 1:
            fmtprint(f, "%T%T", n->u[0].p, n->u[1].p);
            break;
        default:
            fmtprint(f, "(%T", n->u[0].p);
            while ((n = n->u[1].p) != NULL) {
                assert(n->kind == nList);
                fmtprint(f, " %T", n->u[0].p);
            }
            fmtputc(f, ')');
        }
        return FALSE;

    case nInt:
        fmtprint(f, "%ld", n->u[0].i);
        return FALSE;

    case nFloat:
        fmtprint(f, "%f", n->u[0].f);
        return FALSE;

    case nArith:
        fmtprint(f, "`(%T)", n->u[0].p);
        return FALSE;

    case nCmp: {
        char *cmp = n->u[0].s;
        Tree *operand = n->u[1].p;
        for (; operand->u[1].p != NULL; operand = operand->u[1].p)
            fmtprint(f, "%T %s ", operand->u[0].p, cmp);
        fmtprint(f, "%T", operand);
        return FALSE;
    }

    case nOp: {
        char *op = n->u[0].s;
        int op_prec = (*op == '%' ? 3 : *op == '*' || *op == '/' ? 2 : 1);
        Tree *operand = n->u[1].p;
        if (operand->u[0].p->kind == nOp) {
            char *k = operand->u[0].p->u[0].s;
            int k_prec = (*k == '*' || *k == '/' ? 2 : 1);
            if (k_prec < op_prec)
                fmtprint(f, "(%T)", operand->u[0].p);
            else
                fmtprint(f, "%T", operand->u[0].p);
        } else {
            fmtprint(f, "%T", operand->u[0].p);
        }
        for (operand = operand->u[1].p; operand != NULL; operand = operand->u[1].p) {
            int k = operand->u[0].p->kind;
            if (k == nOp) {
                char *k = operand->u[0].p->u[0].s;
                int k_prec = (*k == '*' || *k == '/' ? 2 : 1);;
                if (k_prec <= op_prec)
                    fmtprint(f, " %s (%T)", op, operand->u[0].p);
                else
                    fmtprint(f, " %s %T", op, operand->u[0].p);
            } else {
                fmtprint(f, " %s %T", op, operand->u[0].p);
            }
        }
        return FALSE;
    }

    default:
        panic("conv: bad node kind: %d", n->kind);

    }

    NOTREACHED;
    return FALSE;
}

/* enclose -- build up a closure */
static void enclose(Format *f, Binding *binding, const char *sep) {
    if (binding != NULL) {
        Binding *next = binding->next;
        enclose(f, next, "; ");
        if (binding->defn == NULL) {
            fmtprint(f, "%S%s", binding->name, sep);
        } else {
            fmtprint(f, "%S = %#L%s", binding->name, binding->defn, " ", sep);
        }
    }
}

/* %C -- print a closure */
static Boolean Cconv(Format *f) {
    Closure *closure = va_arg(f->args, Closure *);
    Tree *tree = closure->tree;
    Binding *binding = closure->binding;
    Boolean altform = (f->flags & FMT_altform) != 0;

    if (altform)
        fmtprint(f, "%S", str("%C", closure));
    else {
        if (binding != NULL) {
            fmtprint(f, "%%closure (");
            enclose(f, binding, "");
            fmtprint(f, ") ");
        }
        fmtprint(f, "%T", tree);
    }

    return FALSE;
}

/* %E -- print a term */
static Boolean Econv(Format *f) {
    Term *term = va_arg(f->args, Term *);
    Closure *closure = getclosure(term);

    if (closure != NULL)
        fmtprint(f, (f->flags & FMT_altform) ? "%#C" : "%C", closure);
    else
        fmtprint(f, (f->flags & FMT_altform) ? "%S" : "%s", getstr(term));
    return FALSE;
}

/* %S -- print a string with conservative quoting rules */
static Boolean Sconv(Format *f) {
    int c;
    enum { Begin, Quoted, Unquoted } state = Begin;
    const unsigned char *s, *t;
    extern const char nw[];

    s = va_arg(f->args, const unsigned char *);
    if (f->flags & FMT_altform || *s == '\0')
        goto quoteit;
    for (t = s; (c = *t) != '\0'; t++)
        if (nw[c] || c == '@')
            goto quoteit;
    fmtprint(f, "%s", s);
    return FALSE;

quoteit:

    for (t = s; (c = *t); t++)
        if (!isprint(c)) {
            if (state == Quoted)
                fmtputc(f, '\'');
            if (state != Begin)
                fmtputc(f, '^');
            switch (c) {
                case '\a':  fmtprint(f, "\\a"); break;
                case '\b':  fmtprint(f, "\\b"); break;
                case '\f':  fmtprint(f, "\\f"); break;
                case '\n':  fmtprint(f, "\\n"); break;
                case '\r':  fmtprint(f, "\\r"); break;
                case '\t':  fmtprint(f, "\\t"); break;
                    case '\33': fmtprint(f, "\\e"); break;
                default:    fmtprint(f, "\\%o", c); break;
            }
            state = Unquoted;
        } else {
            if (state == Unquoted)
                fmtputc(f, '^');
            if (state != Quoted)
                fmtputc(f, '\'');
            if (c == '\'')
                fmtputc(f, '\'');
            fmtputc(f, c);
            state = Quoted;
        }

    switch (state) {
        case Begin:
        fmtprint(f, "''");
        break;
        case Quoted:
        fmtputc(f, '\'');
        break;
        case Unquoted:
        break;
    }

    return FALSE;
}

/* %Z -- print a StrList */
static Boolean Zconv(Format *f) {
    StrList *lp, *next;
    char *sep;

    lp = va_arg(f->args, StrList *);
    sep = va_arg(f->args, char *);
    for (; lp != NULL; lp = next) {
        next = lp->next;
        fmtprint(f, "%s%s", lp->str, next == NULL ? "" : sep);
    }
    return FALSE;
}

/* %F -- protect an exported name from brain-dead shells */
static Boolean Fconv(Format *f) {
    int c;
    unsigned char *name, *s;

    name = va_arg(f->args, unsigned char *);

    for (s = name; (c = *s) != '\0'; s++)
        if ((s == name ? isalpha(c) : isalnum(c))
            || (c == '_' && s[1] != '_'))
            fmtputc(f, c);
        else
            fmtprint(f, "__%02x", c);
    return FALSE;
}

/* %N -- undo %F */
static Boolean Nconv(Format *f) {
    int c;
    unsigned char *s = va_arg(f->args, unsigned char *);

    while ((c = *s++) != '\0') {
        if (c == '_' && *s == '_') {
            static const char hexchar[] = "0123456789abcdef";
            const char *h1 = strchr(hexchar, s[1]);
            const char *h2 = strchr(hexchar, s[2]);
            if (h1 != NULL && h2 != NULL) {
                c = ((h1 - hexchar) << 4) | (h2 - hexchar);
                s += 3;
            }
        }
        fmtputc(f, c);
    }
    return FALSE;
}

/* %W -- print a list for exporting to the environment, merging and quoting */
static Boolean Wconv(Format *f) {
    List *lp, *next;

    for (lp = va_arg(f->args, List *); lp != NULL; lp = next) {
        int c;
        const char *s;
        for (s = getstr(lp->term); (c = *s) != '\0'; s++) {
            if (c == ENV_ESCAPE || c == ENV_SEPARATOR)
                fmtputc(f, ENV_ESCAPE);
            fmtputc(f, c);
        }
        next = lp->next;
        if (next != NULL)
            fmtputc(f, ENV_SEPARATOR);
    }
    return FALSE;
}


#if LISPTREES
static Boolean Bconv(Format *f) {
    Tree *n = va_arg(f->args, Tree *);
    if (n == NULL) {
        fmtprint(f, "nil");
        return FALSE;
    }
    switch (n->kind) {

    case nWord:
        fmtprint(f, "(word \"%s\")", n->u[0].s);
        break;

    case nQword:
        fmtprint(f, "(qword \"%s\")", n->u[0].s);
        break;

    case nInt:
        fmtprint(f, "(int %ld)", n->u[0].i);
        break;

    case nFloat:
        fmtprint(f, "(float %f)", n->u[0].f);
        break;

    case nArith:
        fmtprint(f, "(arith %B)", n->u[0].p);
        break;

    case nOp:
        fmtprint(f, "(op \"%s\" %B)", n->u[0].s, n->u[1].p);
        break;

    case nCmp:
        fmtprint(f, "(cmp \"%s\" %B)", n->u[0].s, n->u[1].p);
        break;

    case nPrim:
        fmtprint(f, "(prim %s)", n->u[0].s);
        break;

    case nCall:
        fmtprint(f, "(call %B)", n->u[0].p);
        break;

    case nVar:
        fmtprint(f, "(var %B)", n->u[0].p);
        break;

    case nAssign:
        fmtprint(f, "(assign %B %B)", n->u[0].p, n->u[1].p);
        break;

    case nConcat:
        fmtprint(f, "(concat %B %B)", n->u[0].p, n->u[1].p);
        break;

    case nClosure:
        fmtprint(f, "(%%closure %B %B)", n->u[0].p, n->u[1].p);
        break;

    case nFor:
        fmtprint(f, "(for %B %B)", n->u[0].p, n->u[1].p);
        break;

    case nLambda:
        fmtprint(f, "(lambda %B %B)", n->u[0].p, n->u[1].p);
        break;

    case nLet:
        fmtprint(f, "(let %B %B)", n->u[0].p, n->u[1].p);
        break;

    case nLocal:
        fmtprint(f, "(local %B %B)", n->u[0].p, n->u[1].p);
        break;

    case nMatch:
        fmtprint(f, "(match %B %B)", n->u[0].p, n->u[1].p);
        break;

    case nExtract:
        fmtprint(f, "(extract %B %B)", n->u[0].p, n->u[1].p);
        break;

    case nRedir:
        fmtprint(f, "(redir %B %B)", n->u[0].p, n->u[1].p);
        break;

    case nVarsub:
        fmtprint(f, "(varsub %B %B)", n->u[0].p, n->u[1].p);
        break;

    case nPipe:
        fmtprint(f, "(pipe %d %d)", n->u[0].i, n->u[1].i);
        break;

    case nList: {
        fmtprint(f, "(list");
        do {
            assert(n->kind == nList);
            fmtprint(f, " %B", n->u[0].p);
        } while ((n = n->u[1].p) != NULL);
        fmtprint(f, ")");
        break;
    }

    }
    return FALSE;
}
#endif

/* install the conversion routines */
void initconv(void) {
    fmtinstall('C', Cconv);
    fmtinstall('E', Econv);
    fmtinstall('F', Fconv);
    fmtinstall('L', Lconv);
    fmtinstall('N', Nconv);
    fmtinstall('S', Sconv);
    fmtinstall('T', Tconv);
    fmtinstall('W', Wconv);
    fmtinstall('Z', Zconv);
#if LISPTREES
    fmtinstall('B', Bconv);
#endif
}
