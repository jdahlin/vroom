/* Resetting all per-link state so the linker can run again in one process. */

/* all per-link state, so ld_main can be called again (vccd, VLD_REPEAT) */
static void reset_state(void)
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
	memset(&orphans, 0, sizeof orphans);
	memset(&thunks, 0, sizeof thunks);
	memset(&thunksecs, 0, sizeof thunksecs);
	memset(&phdrs, 0, sizeof phdrs);
	memset(&phsecs, 0, sizeof phsecs);
	memset(&outsecs, 0, sizeof outsecs);
	memset(&wjobs, 0, sizeof wjobs);
	memset(&assert_fail, 0, sizeof assert_fail);
	memset(&recorded, 0, sizeof recorded);
	memset(&script_new, 0, sizeof script_new);
	memset(&routs, 0, sizeof routs);
	/* in the arena, which free_arenas unmapped after the last link (vccd runs many) */
	ncache = NULL;
	ncmask = ncount = 0;
	memset(&ehfdes, 0, sizeof ehfdes);
	memset(&stop_syms, 0, sizeof stop_syms);
	ehhdr_isec = NONE;	/* set only while NONE */
	has_tls = headers_loaded = false;
	tls_tp = tbss_off = 0;
	stub_names = NULL;
	nstubs = 0;
	tr_keys = NULL;
	tr_vals = NULL;
	tr_mask = tr_n = 0;
	entry_name = NULL;
	reladyn_isec = buildid_isec = comment_isec = got_isec = NONE;
	dynsym_isec = dynstr_isec = hash_isec = gnuhash_isec = NONE;
	versym_isec = verdef_isec = dynamic_isec = NONE;
	memset(&sphdrs, 0, sizeof sphdrs);
	memset(&vernodes, 0, sizeof vernodes);
	memset(&ver_locals, 0, sizeof ver_locals);
	memset(&dsyms, 0, sizeof dsyms);
	memset(&dynstr, 0, sizeof dynstr);
	memset(&dynstr_offs, 0, sizeof dynstr_offs);
	have_phdrs = dyn_live = false;
	nverdefs = 0;
	hdr_va = 0;
	memset(&gotsyms, 0, sizeof gotsyms);	/* (all of it: its buffer was in the last link's arena) */
	memset(&gotlocs, 0, sizeof gotlocs);
	symtab_isec = strtab_isec = shstrtab_isec = NONE;
	reladyn_buf = NULL;
	out = NULL;
	ndyn = 0;
	cur_provide = NULL;
	gouts = NULL;
	ngouts = 0;
	atomic_store(&nerrors, 0);
	error_limit = 20;
	timing = getenv("VLD_TIME") != NULL;
}
