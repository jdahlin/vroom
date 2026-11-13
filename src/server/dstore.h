/*
 * dstore.h: vccd's diagnostics store, which keeps every TU's diagnostics across a build; dstore.c
 * has the model.
 *
 * dstore_begin/dstore_end bracket one compile job (the job's sink goes into ppd_env.dsink), so a job
 * that is repeated or fails has its records replaced or removed.  The store is shared by all worker
 * threads and one mutex.  dstore_new_build is called by gen_enter when a new make starts, and
 * dstore_query answers `vcc --vccd-diagnostics[=summary|json]`.
 */
#ifndef VCCD_DSTORE_H
#define VCCD_DSTORE_H
#include "frontend/pp/pp.h"

/* one compile job: its sink (dedup when the client's VCC_DIAG_DEDUP is set) */
typedef struct dstore_job {
	pp_dsink sink;
	char *tu;		/* the source file, absolute */
	char *json;		/* this TU's records, one per line, each with "tu" */
	size_t njson, cjson;
	struct drec_s *recs;	/* level, flag and file of each, for the summary */
	unsigned nrec, crec;
	unsigned omitted;	/* records past the JSON budget: counted only */
} dstore_job;

void dstore_begin(dstore_job *j, const char *cwd, const char *src, int dedup);
/* the job is over: ok keeps its records in place of the TU's earlier ones; a job
   that failed (the TU goes to clang) removes them */
void dstore_end(dstore_job *j, int ok);
/* a new build (make's process group changed): the dedup set starts empty */
void dstore_new_build(void);
/* vcc --vccd-diagnostics[=summary|json]: the answer to fd */
int dstore_query(const char *what, int fd, int errfd);
#endif
