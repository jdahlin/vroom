/* Resetting all per-link state so the linker can run again in one process. */

/* the per-link state not yet in struct ld_context (ld-internal.h), so ld_main
   can be called again (vccd, VLD_REPEAT) */
static void ld_state_reset(void)
{
	memset(&opt, 0, sizeof opt);
	memset(&objs, 0, sizeof objs);
	memset(&isecs, 0, sizeof isecs);
	memset(&mergeins, 0, sizeof mergeins);
	memset(&merges, 0, sizeof merges);
	memset(&gorder, 0, sizeof gorder);
	memset(&isds, 0, sizeof isds);
	memset(&osecs, 0, sizeof osecs);
	memset(&topcmds, 0, sizeof topcmds);
	memset(&refsyms, 0, sizeof refsyms);
	memset(&provides, 0, sizeof provides);
	memset(&pending, 0, sizeof pending);
	/* the lazy-archive pass (its vectors are in the arena) */
	free(lsyms.v);
	memset(&lsyms, 0, sizeof lsyms);
	memset(&lcand, 0, sizeof lcand);
	memset(&lout, 0, sizeof lout);
	memset(&ltaken, 0, sizeof ltaken);
	memset(&assert_fail, 0, sizeof assert_fail);
	memset(&recorded, 0, sizeof recorded);
	memset(&script_new, 0, sizeof script_new);
	/* in the arena, which ld_regions_free unmapped after the last link (vccd runs many) */
	memset(&stop_syms, 0, sizeof stop_syms);
	x86_ld = false;
	atomic_store(&ld_machine, 0);
	stub_names = NULL;
	nstubs = 0;
	entry_name = NULL;
	memset(&sphdrs, 0, sizeof sphdrs);
	memset(&vernodes, 0, sizeof vernodes);
	memset(&ver_locals, 0, sizeof ver_locals);
	have_phdrs = false;
	nverdefs = 0;
	cur_provide = NULL;
}
