/* ld_main: the entry point vccd and vlink call for one link. */

int ld_main(int argc, char **argv, int dirfd, FILE *err)
{
	struct rspmem *rsp = NULL;
	rsp_expand(&argc, &argv, dirfd, &rsp);
	int rc;
	if (first_input_macho(argc, argv, dirfd)) {
		rc = ld_macho(argc, argv, dirfd, err);
	} else {
		VLOCK(ldlock);
		errf = err ? err : stderr;
		dirfd_base = dirfd;
		rc = run(argc, argv);
		free_arenas();
	}
	while (rsp) {
		struct rspmem *nx = rsp->next;
		free(rsp);
		rsp = nx;
	}
	return rc;
}
