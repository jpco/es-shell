/* linenoise.c -- line-editing with the linenoise library */

#include "es.h"
#include "prim.h"

#include <linenoise.h>

static char *history = NULL;

PRIM(linenoise) {
	char *prompt = list == NULL ? mprint("") : mprint("%s", getstr(list->term));
	char *line = linenoise(prompt);
	if (line == NULL)
		return NULL;
	List *result = mklist(mkstr(str("%s", line)), NULL);
	efree(prompt);
	efree(line);
	return result;
}

PRIM(sethistory) {
	static Boolean initialized = FALSE;
	if (!initialized) {
		globalroot(&history);
		initialized = TRUE;
	}
	if (list == NULL) {
		history = NULL;
	} else {
		history = getstr(list->term);
		linenoiseHistoryLoad(history);
	}
	return list;
}

PRIM(writehistory) {
	if (list == NULL)
		return ltrue;
	linenoiseHistoryAdd(getstr(list->term));
	if (history != NULL)
		linenoiseHistorySave(history);
	return ltrue;
}

PRIM(setmaxhistorylength) {
	int len = 1000;
	if (list != NULL) {
		char *s;
		len = (int)strtol(getstr(list->term), &s, 0);
		if (len < 0 || (s != NULL && *s != '\0'))
			fail("$&setmaxhistorylength", "you did it wrong");
	}
	linenoiseHistorySetMaxLen(len);
	return list;
}

extern Dict *initprims_linenoise(Dict *primdict) {
	X(linenoise);
	X(sethistory);
	X(writehistory);
	X(setmaxhistorylength);
	return primdict;
}
