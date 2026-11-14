#ifndef VCC_CXX_H
#define VCC_CXX_H
/*
 * The C++ front end (PLAN-cxx.md).  The driver calls cxx_run instead of
 * cc_run for a C++ job.  Today it parses (with name lookup) and reports how
 * far it got; it does not generate code yet.
 */
typedef struct pp pp;
typedef struct cc_opts cc_opts;

typedef struct cxx_options {
	int standard;		/* 98 .. 26 */
	int errfd;
	int progress;		/* print "cxx: parsed N/M tokens" (bench/cxx-json.py) */
} cxx_options;

/* 0 if the TU parsed without errors; co: the target and code options, as for C */
int cxx_run(pp *p, const cc_opts *co, const cxx_options *o);
#endif
