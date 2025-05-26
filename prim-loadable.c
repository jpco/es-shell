/* prim-loadable.c -- demo of a loadable primitives file */

#include "es.h"
#include "prim.h"

/*
 * Compile this to a shared object with something like
 *
 *     ; gcc -shared -fPIC -o loadable.so prim-loadable.c
 *
 * and then load it into es with something like
 *
 *     ; $&loadprims ./loadable.so
 *
 * and try it out with
 *
 *     ; echo <=$&getcwd
 *
 * Note that for this file to have access to all the executable's symbols
 * that it needs for the core data structures and everything, the executable
 * may need to be linked with -rdynamic or equivalent.
 */

PRIM(getcwd) {
	/* NOTE: this use of getcwd is a GNU extension. */
	char *wd = getcwd(NULL, 0);
	if (wd == NULL)
		fail("$&getcwd", "%s", esstrerror(errno));
	return mklist(mkstr(wd), NULL);
}

Dict *init(Dict *primdict) {
	X(getcwd);
	return primdict;
}
