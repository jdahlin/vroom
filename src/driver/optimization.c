/*
 * optimization.c: the -O levels and the options that turn one optimization on or off.
 *
 * A level is a number, 0 to 3 (driver_optimization_level), and what it turns on is the table
 * CC_OPTIMIZATIONS (frontend/c/cc.h): a row is on from its level up, unless -fNAME or
 * -fno-NAME says otherwise (the last one given wins, wherever -O stands).  doc/OPTIMIZATION.md
 * is the same in prose, with what a level contains that has no option.
 *
 *   -O0            nothing: the direct emitter, every local in its frame slot
 *   -O1 -Og -O     the direct emitter with its folds and record clean-ups
 *   -O2 -Os -Oz    functions go through VIR (codegen/vir/); what VIR cannot build is -O1's code
 *   -O3 -Ofast     ... with the loop vectorizer
 *
 * The -fvir-* options (cc_vir_aids) are the developer's aids, taken here too: they select
 * nothing a user needs and are not in vccd's capability table.
 */
#include "driver-internal.h"

enum {	/* a row's index */
#define X(field, name, level) DRIVER_OPT_##field,
	CC_OPTIMIZATIONS(X)
#undef X
};

static const struct {
	const char *name;
	int level;
} driver_optimizations[CC_OPTIMIZATION_COUNT] = {
#define X(field, name, level) { name, level },
	CC_OPTIMIZATIONS(X)
#undef X
};

/* gcc's names for a row, which clang takes too */
static const struct {
	const char *alias;
	int row;
} driver_optimization_aliases[] = {
	{ "tree-vectorize", DRIVER_OPT_vectorize },
};

/* the level of -O<s>: clang's reading (-O is -O1, a number above 3 is 3) */
VPRIV int driver_optimization_level(const char *s)
{
	if (!*s || !strcmp(s, "1") || !strcmp(s, "g"))
		return 1;
	if (!strcmp(s, "0"))
		return 0;
	if (!strcmp(s, "2") || !strcmp(s, "s") || !strcmp(s, "z"))
		return 2;
	return 3;
}

static int driver_optimization_find(const char *name)
{
	for (int i = 0; i < CC_OPTIMIZATION_COUNT; i++)
		if (!strcmp(name, driver_optimizations[i].name))
			return i;
	for (size_t i = 0; i < sizeof driver_optimization_aliases / sizeof *driver_optimization_aliases; i++)
		if (!strcmp(name, driver_optimization_aliases[i].alias))
			return driver_optimization_aliases[i].row;
	return -1;
}

/* a is an option of this file: it is recorded in job (a bad value: job->bad_value_option); false: not one */
VPRIV bool driver_optimization_option(struct driver_job *job, const char *a)
{
	cc_vir_aids *v = &job->vir_aid;
	const char *name;
	bool no;
	int row;

	if (strncmp(a, "-f", 2))
		return false;
	no = !strncmp(a + 2, "no-", 3);
	name = a + (no ? 5 : 2);
	if (!strcmp(name, "vir-lower"))
		v->build_only = no;
	else if (!strcmp(name, "vir-verify"))
		v->verify = !no;
	else if (!strcmp(name, "vir-dump"))
		v->dump = !no;
	else if (!strcmp(name, "vir-report"))
		v->report = !no;
	else if (!strcmp(name, "vir-check"))
		v->check = !no;
	else if (!strcmp(name, "vir-stats"))
		v->stats = !no;
	else if (!strcmp(name, "vir-vectorize-report"))
		v->vectorize_report = !no;
	else if (!no && !strncmp(name, "vir-ccmp=", 9)) {
		const char *m = name + 9;

		v->ccmp = !strcmp(m, "all") ? CC_CCMP_ALL : !strcmp(m, "none") ? CC_CCMP_NONE : CC_CCMP_SELECT;
		if (v->ccmp == CC_CCMP_SELECT && strcmp(m, "select"))
			job->bad_value_option = a;
	} else if (!no && !strncmp(name, "vir-probe=", 10)) {
		char *end;
		long mask = strtol(name + 10, &end, 0);

		if (end == name + 10 || *end || mask < 0 || mask > 255)
			job->bad_value_option = a;
		v->probe = (int)mask;
	} else if (!no && !strncmp(name, "vir-passes=", 11))
		v->passes = name + 11;
	else if (!no && !strncmp(name, "vir-functions=", 14))
		v->functions = name + 14;
	else if ((row = driver_optimization_find(name)) >= 0)
		job->optimization[row] = no ? -1 : 1;
	else
		return false;
	return true;
}

/* what the job's level and options turn on */
VPRIV cc_optimizations driver_optimization_resolve(const struct driver_job *job)
{
	bool on[CC_OPTIMIZATION_COUNT];
	bool vir;

	for (int i = 0; i < CC_OPTIMIZATION_COUNT; i++)
		on[i] = job->optimization[i] ? job->optimization[i] > 0 : job->optimization_level >= driver_optimizations[i].level;
	/* VIR is -O2's code generator: an option does not bring it to a lower level, and without
	   it there is nothing for the other rows to work on */
	vir = on[DRIVER_OPT_vir] && job->optimization_level >= 2;
	return (cc_optimizations){
#define X(field, name, level) .field = vir && on[DRIVER_OPT_##field],
		CC_OPTIMIZATIONS(X)
#undef X
	};
}
