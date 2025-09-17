/* locale.c -- locale handling */

#include <locale.h>

#include "es.h"

#define LC_PRESENT	(1 << 19)
#define PTRSTUFF(x)	((void *)((x)|LC_PRESENT))
#define PTREXTRACT(x)	(((int)(x))&~LC_PRESENT)


static Dict *localedict = NULL;

static void initlocaledict(void) {
	localedict = mkdict();
	localedict = dictput(localedict, "LC_ALL", PTRSTUFF(LC_ALL));
#ifdef LC_ADDRESS
	localedict = dictput(localedict, "LC_ADDRESS", PTRSTUFF(LC_ADDRESS));
#endif
	localedict = dictput(localedict, "LC_COLLATE", PTRSTUFF(LC_COLLATE));
	localedict = dictput(localedict, "LC_CTYPE", PTRSTUFF(LC_CTYPE));
#ifdef LC_IDENTIFICATION
	localedict = dictput(localedict, "LC_IDENTIFICATION", PTRSTUFF(LC_IDENTIFICATION));
#endif
#ifdef LC_MEASUREMENT
	localedict = dictput(localedict, "LC_MEASUREMENT", PTRSTUFF(LC_MEASUREMENT));
#endif
	localedict = dictput(localedict, "LC_MESSAGES", PTRSTUFF(LC_MESSAGES));
	localedict = dictput(localedict, "LC_MONETARY", PTRSTUFF(LC_MONETARY));
#ifdef LC_NAME
	localedict = dictput(localedict, "LC_NAME", PTRSTUFF(LC_NAME));
#endif
	localedict = dictput(localedict, "LC_NUMERIC", PTRSTUFF(LC_NUMERIC));
#ifdef LC_PAPER
	localedict = dictput(localedict, "LC_PAPER", PTRSTUFF(LC_PAPER));
#endif
#ifdef LC_TELEPHONE
	localedict = dictput(localedict, "LC_TELEPHONE", PTRSTUFF(LC_TELEPHONE));
#endif
	localedict = dictput(localedict, "LC_TIME", PTRSTUFF(LC_TIME));
}

extern List *setlocalelang(List *value) {
	extern char **environ;
	char **e;
	Push p, p2;

	varpush(&p, "set-LANG", NULL);
	varpush(&p2, "LANG", value);

	e = environ;
	environ = mkenv()->vector;
	setlocale(LC_ALL, "");
	environ = e;

	varpop(&p2);
	varpop(&p);
	return ltrue;
}

extern List *setlocalecategory(char *category0, List *value0) {
	char *r;
	void *v;
	List *result;

	Ref(char *, category, category0);
	Ref(char *, value, str("%L", value0, " "));

	v = dictget(localedict, category);
	if (v == NULL)
		fail("$&setlocale", "bad locale category: %s", category);

	r = setlocale(PTREXTRACT(v), value);
	if (r != NULL) {
		Term *t = mkstr(r);
		result = mklist(t, NULL);
	} else
		fail("$&setlocale", "bad locale value: %s", value);

	RefEnd2(value, category);
	return result;
}


/*
 * initialization
 */

extern void initlocale(void) {
	globalroot(&localedict);
	initlocaledict();
	setlocale(LC_ALL, "");
}
