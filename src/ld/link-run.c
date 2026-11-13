/* run(): the passes of one ELF link, in order. */

static int ld_relocatable(void);

static int run(int argc, char **argv)
{
	reset_state();
	{
		int n = 0;
		size_t len = sizeof n;
#ifdef __APPLE__
		if (!sysctlbyname("hw.perflevel0.physicalcpu", &n, &len, NULL, 0) && n > 0)
			nthreads = n;
#else
		(void)len;
		if ((n = (int)sysconf(_SC_NPROCESSORS_ONLN)) > 0)
			nthreads = n;
#endif
		else
			nthreads = 4;
		if (nthreads > 64)
			nthreads = 64;
	}
	parse_args(argc, argv);
	if (atomic_load(&nerrors))
		return 1;
	double t0 = now();
	t_last = t0;
	p_last = vperf_now();
	flt_last = timing ? minflt() : 0;
	pic = opt.shared;

	load_inputs();
	phase("map+parse inputs");
	if (atomic_load(&nerrors))
		return 1;
	gtable_init();
	if (opt.script) {
		size_t n;
		uint8_t *b = map_file(opt.script, &n);
		if (!b)
			return 1;
		parse_script((const char *)b, n);
		if (atomic_load(&nerrors))
			return 1;
	}
	alloc_isecs(0);
	pfor(objs.n, fill_isecs, NULL);
	dedup_comdats();
	drop_gnu_property();
	pfor(objs.n, resolve_obj, NULL);
	pfor(objs.n, check_dups, NULL);
	phase("resolve symbols");
	if (atomic_load(&nerrors))
		return 1;
	if (opt.relocatable)
		return ld_relocatable();

	if (opt.stubs) {
		collect_stubs();
		if (nstubs) {
			Obj *o = make_stub_obj(stub_names, nstubs);
			o->idx = objs.n;
			VVEC_PUSH(objs, o, alloc);
			alloc_isecs(o->idx);
			fill_isecs(NULL, o->idx);
			resolve_obj(NULL, o->idx);
			write_stub_list();
		}
	}
	nobj_real = objs.n;
	script_symbols();
	build_gorder();
	pfor(gorder.n, finish_syms, NULL);
	assign_versions();
	phase("script symbols, order");

	if (opt.gc && !opt.relocatable)
		gc_sections(opt.entry ? opt.entry : entry_name ? entry_name : "_start");
	make_commons();
	add_synthetic_sections();
	assign_sections();
	/* commit sections into their output sections, in order */
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != C_OSEC)
			continue;
		OSec *os = &osecs.v[topcmds.v[c].idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == C_ISD) {
				ISD *d = &isds.v[os->cmds.v[k].idx];
				for (uint32_t s = 0; s < d->secs.n; s++)
					commit_section(os, &isecs.v[d->secs.v[s]]);
			}
	}
	define_dynamic_sym();
	start_stop_symbols();
	pfor(gorder.n, demote_symbols, NULL);
	report_undefined();
	phase("assign sections");
	finalize_merges();
	phase("merge sections");
	if (atomic_load(&nerrors))
		return 1;

	process_symbol_assignments();
	adjust_output_sections();
	if (got_isec != NONE)
		for (uint32_t i = 0; i < objs.n; i++)
			if (objs.v[i]->buf && objs.v[i]->firstg)
				objs.v[i]->lgot = calloc(objs.v[i]->firstg, 1);
	pfor(isecs.n, x86_ld ? x64_scan_relocs : scan_relocs, NULL);
	/* .got: one slot per symbol, globals in symbol id order, then the locals by
	 * file (lld postScanRelocations) */
	for (uint32_t g = 0, ng = atomic_load(&ngsyms); g < ng; g++)
		if (atomic_load(&gsyms[g].needgot)) {
			gsyms[g].gotidx = gotsyms.n;
			VVEC_PUSH(gotsyms, g, alloc);
			if (pic && got_isec != NONE && !gsym_isabs(g) && gsyms[g].state != GS_UNDEF && !gsyms[g].demoted)
				isecs.v[got_isec].ndyn++;
		}
	for (uint32_t i = 0; got_isec != NONE && i < objs.n; i++) {
		Obj *o = objs.v[i];

		for (uint32_t k = 0; o->lgot && k < o->firstg; k++)
			if (o->lgot[k]) {
				if (!o->lgotidx)
					o->lgotidx = alloc(o->firstg * sizeof(uint32_t));
				o->lgotidx[k] = gotsyms.n + gotlocs.n;
				VVEC_PUSH(gotlocs, ((GotLoc){i, k}), alloc);
				if (pic && o->sym[k].st_shndx != SHN_ABS)
					isecs.v[got_isec].ndyn++;
			}
	}
	if (got_isec != NONE)
		isecs.v[got_isec].size = (uint64_t)(gotsyms.n + gotlocs.n) * 8;
	ndyn = 0;
	for (uint32_t i = 0; i < isecs.n; i++)
		ndyn += isecs.v[i].ndyn;
	if (reladyn_isec != NONE)
		isecs.v[reladyn_isec].size = ndyn * sizeof(Rela);
	if (reladyn_isec != NONE && !ndyn && sec_live(reladyn_isec)) {
		/* lld drops an empty .rela.dyn (RelocationSection::isNeeded) */
		ISD *d = &isds.v[isecs.v[reladyn_isec].isd];
		uint32_t w = 0;
		for (uint32_t k = 0; k < d->secs.n; k++)
			if (d->secs.v[k] != reladyn_isec)
				d->secs.v[w++] = d->secs.v[k];
		d->secs.n = w;
		isecs.v[reladyn_isec].osec = NONE;
		adjust_output_sections();
	}
	build_dynamic();
	phase("scan relocations");
	if (atomic_load(&nerrors))
		return 1;

	/* room for thunk sections without moving isecs */
	if (isecs.cap < isecs.n + 4096) {
		ISec *nv = alloc((size_t)(isecs.n + 4096) * sizeof(ISec));
		memcpy(nv, isecs.v, isecs.n * sizeof(ISec));
		isecs.v = nv;
		isecs.cap = isecs.n + 4096;
	}
	/* output sections in header order (live ones, script order) */
	outsecs.n = 0;
	for (uint32_t c = 0; c < topcmds.n; c++)
		if (topcmds.v[c].kind == C_OSEC && osecs.v[topcmds.v[c].idx].live) {
			VVEC_PUSH(outsecs, topcmds.v[c].idx, alloc);
			osecs.v[topcmds.v[c].idx].shidx = outsecs.n;
		}

	assign_addresses();
	int pass = 0;
	for (;;) {
		bool changed = create_thunks(pass);
		pass++;
		if (changed && pass >= 30) {
			error("address assignment did not converge");
			break;
		}
		bool chg2 = assign_addresses();
		if (!changed && !chg2)
			break;
	}
	in_layout = true;
	assign_addresses();
	in_layout = false;
	for (uint32_t i = 0; i < stop_syms.n; i++)
		gsyms[stop_syms.v[i]].ev.val = osecs.v[gsyms[stop_syms.v[i]].ev.idx].size;
	for (uint32_t i = 0; i < recorded.n; i++)
		error("%s", recorded.v[i]);
	for (uint32_t i = 0; i < assert_fail.n; i++)
		error("%s", assert_fail.v[i]);
	phase("layout");
	if (atomic_load(&nerrors))
		return 1;

	/* drop empty thunk sections (none are kept by construction) */
	if (have_phdrs) {
		create_script_phdrs();
	} else {
		create_phdrs();
		finish_phdrs();
	}
	/* symtab and shstrtab sizes are needed before file offsets */
	build_shstrtab();
	build_symtab();
	dynamic_shdrs();
	{
		OSec *os = &osecs.v[isec_osec(symtab_isec)];
		os->link = osecs.v[isec_osec(strtab_isec)].shidx;
		os->info = nlocal_out;
		os->entsize = sizeof(Sym);
	}
	/* sizes of the non-alloc synthetic sections changed: re-run layout of them */
	for (uint32_t i = 0; i < outsecs.n; i++) {
		OSec *os = &osecs.v[outsecs.v[i]];
		if (os->flags & SHF_ALLOC)
			continue;
		bool dummy = false;
		uint64_t d = dot;
		dot = 0;
		assign_offsets(outsecs.v[i], &dummy);
		dot = d;
	}
	assign_file_offsets();
	phase("phdrs, symtab sizes, offsets");

	out = bigalloc(filesize);
	/* RELATIVE relocations: per section, in address order */
	if (reladyn_isec != NONE && ndyn) {
		VVEC(uint32_t) dl = {0};
		for (uint32_t i = 0; i < isecs.n; i++)
			if (isecs.v[i].ndyn)
				VVEC_PUSH(dl, i, alloc);
		qsort(dl.v, dl.n, sizeof(uint32_t), cmp_dyn);
		uint32_t at = 0;
		for (uint32_t i = 0; i < dl.n; i++) {
			isecs.v[dl.v[i]].dyn0 = at;
			at += isecs.v[dl.v[i]].ndyn;
		}
		/* built in place in the output image */
		reladyn_buf = (Rela *)(void *)(out + osecs.v[isec_osec(reladyn_isec)].offset +
					       isecs.v[reladyn_isec].off);
	}
	wjobs.n = 0;
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != C_OSEC || !osecs.v[topcmds.v[c].idx].live)
			continue;
		OSec *os = &osecs.v[topcmds.v[c].idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == C_ISD) {
				ISD *d = &isds.v[os->cmds.v[k].idx];
				for (uint32_t s = 0; s < d->secs.n; s++)
					VVEC_PUSH(wjobs, d->secs.v[s], alloc);
			}
	}
	final_addresses();
	write_fills();
	pfor(wjobs.n, write_isec, NULL);
	write_dynamic();
	write_ehhdr();
	phase("copy + relocate");
	fill_symtab();
	write_synthetic();
	write_headers();
	phase("symtab, headers");
	if (atomic_load(&nerrors))
		return 1;
	write_build_id();
	phase("build id");
	int rc = write_output();
	phase("write");
	if (timing)
		fprintf(errf, "  %-28s %7.2f ms (%u objects, %u sections, %u globals, %llu MB out, %d threads)\n",
			"total", (now() - t0) * 1e3, objs.n, isecs.n, gorder.n,
			(unsigned long long)(filesize >> 20), nthreads);
	unmap(out, filesize);
	for (uint32_t i = 0; i < objs.n; i++)
		if (objs.v[i]->mapped && objs.v[i]->buf)
			unmap(objs.v[i]->buf, objs.v[i]->size);
	gtable_free();
	return rc || atomic_load(&nerrors) ? 1 : 0;
}
