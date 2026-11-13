/* ld_main: the entry point vccd and vlink call for one link. */

int ld_main(int argc, char **argv, int dirfd, FILE *err)
{
	struct ld_response_memory *rsp = NULL;
	ld_response_files_expand(&argc, &argv, dirfd, &rsp);
	int rc;
	if (ld_first_input_is_macho(argc, argv, dirfd)) {
		rc = macho_link(argc, argv, dirfd, err);
	} else {
		vtask_mutex_lock(&ld_link_lock);	/* (a second link waits off the CPU budget) */
		errf = err ? err : stderr;
		dirfd_base = dirfd;
		rc = ld_link_run(argc, argv);
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
