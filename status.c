/* status.c -- status manipulations ($Revision: 1.1.1.1 $) */

#include "es.h"
#include "term.h"

static const Term
	trueterm	= { "0", NULL },
	falseterm	= { "1", NULL };
static const List
	truelist	= { (Term *) &trueterm, NULL },
	falselist	= { (Term *) &falseterm, NULL };
List
	*true		= (List *) &truelist,
	*false		= (List *) &falselist;

/* istrue -- is this status list true? */
extern Boolean istrue(List *status) {
	for (; status != NULL; status = status->next) {
		Term *term = status->term;
		if (term->closure != NULL)
			return FALSE;
		else {
			const char *str = term->str;
			assert(str != NULL);
			if (*str != '\0' && (*str != '0' || str[1] != '\0'))
				return FALSE;
		}
	}
	return TRUE;
}

/* exitstatus -- turn a status list into an exit(2) value */
extern int exitstatus(List *status) {
	Term *term;
	char *s;
	unsigned long n;

	if (status == NULL)
		return 0;
	if (status->next != NULL)
		return istrue(status) ? 0 : 1;
	term = status->term;
	if (term->closure != NULL)
		return 1;

	s = term->str;
	if (*s == '\0')
		return 0;
	n = strtol(s, &s, 0);
	if (*s != '\0' || n > 255)
		return 1;
	return n;
}

/* mkstatus -- turn a unix exit(2) status into a string */
extern char *mkstatus(int status) {
	if (SIFSTOPPED(status)) {
		char *name = signame(SSTOPSIG(status));
		return name;
	}
	if (SIFSTOPPED(status) || SIFSIGNALED(status)) {
		char *name = signame(STERMSIG(status));
		if (SCOREDUMP(status))
			name = str("%s+core", name);
		return name;
	}
	return str("%d", SEXITSTATUS(status));
}

extern Boolean sifsignaled(List *status) {
	Boolean signaled = FALSE;
	Ref(List *, lp, status);
	for (; lp != NULL; lp = lp->next)
		if (signumber(getstr(lp->term)) >= 0) {
			signaled = TRUE;
			break;
		}
	RefEnd(lp);
	return signaled;
}

/* printstatus -- print the status if we should */
static void printstatus(int pid, List *list) {
	Ref(List *, lp, list);
	for (; lp != NULL; lp = lp->next) {
		int signum;
		Ref(char *, status, NULL);

		status = getstr(lp->term);
		signum = signumber(status);
		if (signum > 0) {
			const char *msg = sigmessage(signum), *tail = "";
			if (sigcore(status)) {
				tail = "--core dumped";
				if (*msg == '\0')
					tail += (sizeof "--") - 1;
			}
			if (*msg != '\0' || *tail != '\0') {
				if (pid == 0)
					eprint("%s%s\n", msg, tail);
				else
					eprint("%d: %s%s\n", pid, msg, tail);
			}
		}
		RefEnd(status);
	}
	RefEnd(lp);
}

extern List *reportstatus(List *status, Binding *binding) {
	Ref(List *, report, varlookup("fn-%report-status", binding));
	if (report == NULL) {
		status = status->next->next;
		printstatus(0, status);
	} else
		status = eval(append(report, status), NULL, 0);
	RefEnd(report);
	return status;
}
