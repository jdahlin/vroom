/* ld_main: the entry point vccd and vlink call for one link. */

#include "ld-internal.h"

/* one ELF link at a time in the process */
static pthread_mutex_t ld_link_lock = PTHREAD_MUTEX_INITIALIZER;

int ld_main(int argc, char **argv, int dirfd, FILE *err)
{
	struct ld_response_memory *rsp = NULL;
	ld_response_files_expand(&argc, &argv, dirfd, &rsp);
	int rc;
	if (ld_first_input_is_macho(argc, argv, dirfd)) {
		rc = macho_link(argc, argv, dirfd, err);
	} else {
		FILE *error_stream = err ? err : stderr;

		vtask_mutex_lock(&ld_link_lock);	/* (a second link waits off the CPU budget) */
		struct ld_context *ctx = ld_context_create(error_stream, dirfd);
		if (ctx) {
			rc = ld_link_run(ctx, argc, argv);
			ld_context_destroy(ctx);
		} else {
			fprintf(error_stream, "vld: error: out of memory\n");
			rc = 1;
		}
		ld_regions_free();
		pthread_mutex_unlock(&ld_link_lock);
	}
	while (rsp) {
		struct ld_response_memory *nx = rsp->next;
		free(rsp);
		rsp = nx;
	}
	return rc;
}
