/*
 * diagnostics-store.h: vccd's diagnostics store, which keeps every TU's diagnostics across a build; diagnostics-store.c
 * has the model.
 *
 * vccd_diagnostics_job_begin/vccd_diagnostics_job_end bracket one compile job (the job's sink goes into driver_environment.diagnostic_sink), so a job
 * that is repeated or fails has its records replaced or removed.  The store is shared by all worker
 * threads and one mutex.  vccd_diagnostics_build_new is called by vccd_generation_enter when a new make starts, and
 * vccd_diagnostics_query answers `vcc --vccd-diagnostics[=summary|json]`.
 */
#ifndef VCCD_DSTORE_H
#define VCCD_DSTORE_H
#include "frontend/pp/pp.h"

/* one compile job: its sink (dedup when the client's VCC_DIAG_DEDUP is set) */
typedef struct vccd_diagnostics_job {
	pp_dsink sink;
	char *unit_path;		/* the source file, absolute */
	char *json;		/* this TU's records, one per line, each with "tu" */
	size_t json_length, json_capacity;
	struct vccd_diagnostics_record *records;	/* level, flag and file of each, for the summary */
	unsigned record_count, record_capacity;
	unsigned omitted;	/* records past the JSON budget: counted only */
} vccd_diagnostics_job;

void vccd_diagnostics_job_begin(vccd_diagnostics_job *j, const char *cwd, const char *src, int dedup);
/* the job is over: ok keeps its records in place of the TU's earlier ones; a job
   that failed (the TU goes to clang) removes them */
void vccd_diagnostics_job_end(vccd_diagnostics_job *j, int ok);
/* a new build (make's process group changed): the dedup set starts empty */
void vccd_diagnostics_build_new(void);
/* vcc --vccd-diagnostics[=summary|json]: the answer to fd */
int vccd_diagnostics_query(const char *what, int fd, int errfd);
#endif
