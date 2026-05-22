/* somethingelse.c -- define some other primitives */

#include "es.h"
#include "prim.h"

PRIM(somethingelse) {
	print("AAAAAH %L\n", list, " ");
	return ltrue;
}

extern Dict *initprims_somethingelse(Dict *primdict) {
	X(somethingelse);
	return primdict;
}
