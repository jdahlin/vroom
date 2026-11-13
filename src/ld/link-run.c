/* ld_link_run(): the passes of one ELF link, in order. */

static int ld_relocatable_link(void);

static int ld_link_run(int argc, char **argv)
{
	ld_state_reset();
	ld_thread_count = ld_parallel_default_width();
	ld_arguments_parse(argc, argv);
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;
#ifdef VLD_MAIN
	/* the standalone tool: the process is this link, so --threads is the budget
	   (inside vccd the server's budget stays, and nthreads only caps the width) */
	if (ld_thread_count > vtask_budget())
		vtask_budget_set(ld_thread_count);
#endif
	double t0 = ld_clock_now();
	ctx->diagnostics.phase_start_time = t0;
	ctx->diagnostics.phase_start_counters = vperformance_now();
	ctx->diagnostics.phase_start_faults = ctx->diagnostics.phase_timing ? ld_minor_fault_count() : 0;
	pic = opt.shared;

	ld_inputs_load();
	ld_phase_time_report("map+parse inputs");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;
	ld_global_table_init();
	if (opt.script) {
		size_t n;
		uint8_t *b = ld_file_map(opt.script, &n);
		if (!b)
			return 1;
		ld_script_parse((const char *)b, n);
		if (atomic_load(&ctx->diagnostics.error_count))
			return 1;
	}
	ld_input_sections_alloc(0);
	ld_parallel_for(objs.n, ld_input_sections_fill, NULL);
	ld_comdat_groups_deduplicate();
	ld_gnu_property_drop();
	ld_parallel_for(objs.n, ld_object_symbols_resolve, NULL);
	ld_parallel_for(objs.n, ld_duplicate_symbols_check, NULL);
	ld_phase_time_report("resolve symbols");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;
	if (opt.relocatable)
		return ld_relocatable_link();

	if (opt.stubs) {
		ld_stubs_collect();
		if (nstubs) {
			ld_object *o = ld_stub_object_create(stub_names, nstubs);
			o->idx = objs.n;
			VVEC_PUSH(objs, o, ld_region_alloc);
			ld_input_sections_alloc(o->idx);
			ld_input_sections_fill(NULL, o->idx);
			ld_object_symbols_resolve(NULL, o->idx);
			ld_stub_list_write();
		}
	}
	nobj_real = objs.n;
	ld_script_symbols_declare();
	ld_global_order_build();
	ld_parallel_for(gorder.n, ld_global_symbol_finish_task, NULL);
	ld_symbol_versions_assign();
	ld_phase_time_report("script symbols, order");

	if (opt.gc && !opt.relocatable)
		ld_sections_garbage_collect(opt.entry ? opt.entry : entry_name ? entry_name : "_start");
	ld_common_symbols_replace();
	ld_synthetic_sections_add();
	ld_input_sections_assign();
	/* commit sections into their output sections, in order */
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != LD_COMMAND_OUTPUT_SECTION)
			continue;
		ld_output_section *os = &osecs.v[topcmds.v[c].idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION) {
				ld_input_section_description *d = &isds.v[os->cmds.v[k].idx];
				for (uint32_t s = 0; s < d->secs.n; s++)
					ld_section_commit(os, &isecs.v[d->secs.v[s]]);
			}
	}
	ld_dynamic_symbol_define();
	ld_start_stop_symbols_define();
	ld_parallel_for(gorder.n, ld_symbols_demote, NULL);
	ld_undefined_symbols_report();
	ld_phase_time_report("assign sections");
	ld_merges_finalize();
	ld_phase_time_report("merge sections");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;

	ld_symbol_assignments_process();
	ld_output_sections_adjust();
	if (got_isec != LD_NONE)
		for (uint32_t i = 0; i < objs.n; i++)
			if (objs.v[i]->buf && objs.v[i]->firstg)
				objs.v[i]->lgot = calloc(objs.v[i]->firstg, 1);
	ld_parallel_for(isecs.n, x86_ld ? x64_ld_relocation_scan : a64_ld_relocation_scan, NULL);
	/* .got: one slot per symbol, globals in symbol id order, then the locals by
	 * file (lld postScanRelocations) */
	for (uint32_t g = 0, ng = atomic_load(&ngsyms); g < ng; g++)
		if (atomic_load(&gsyms[g].needgot)) {
			gsyms[g].gotidx = gotsyms.n;
			VVEC_PUSH(gotsyms, g, ld_region_alloc);
			if (pic && got_isec != LD_NONE && !ld_global_symbol_is_absolute(g) && gsyms[g].state != LD_SYMBOL_STATE_UNDEFINED && !gsyms[g].demoted)
				isecs.v[got_isec].ndyn++;
		}
	for (uint32_t i = 0; got_isec != LD_NONE && i < objs.n; i++) {
		ld_object *o = objs.v[i];

		for (uint32_t k = 0; o->lgot && k < o->firstg; k++)
			if (o->lgot[k]) {
				if (!o->lgotidx)
					o->lgotidx = ld_region_alloc(o->firstg * sizeof(uint32_t));
				o->lgotidx[k] = gotsyms.n + gotlocs.n;
				VVEC_PUSH(gotlocs, ((ld_got_location){i, k}), ld_region_alloc);
				if (pic && o->sym[k].st_shndx != SHN_ABS)
					isecs.v[got_isec].ndyn++;
			}
	}
	if (got_isec != LD_NONE)
		isecs.v[got_isec].size = (uint64_t)(gotsyms.n + gotlocs.n) * 8;
	ndyn = 0;
	for (uint32_t i = 0; i < isecs.n; i++)
		ndyn += isecs.v[i].ndyn;
	if (reladyn_isec != LD_NONE)
		isecs.v[reladyn_isec].size = ndyn * sizeof(elf_rela);
	if (reladyn_isec != LD_NONE && !ndyn && ld_input_section_is_live(reladyn_isec)) {
		/* lld drops an empty .rela.dyn (RelocationSection::isNeeded) */
		ld_input_section_description *d = &isds.v[isecs.v[reladyn_isec].isd];
		uint32_t w = 0;
		for (uint32_t k = 0; k < d->secs.n; k++)
			if (d->secs.v[k] != reladyn_isec)
				d->secs.v[w++] = d->secs.v[k];
		d->secs.n = w;
		isecs.v[reladyn_isec].osec = LD_NONE;
		ld_output_sections_adjust();
	}
	ld_dynamic_sections_build();
	ld_phase_time_report("scan relocations");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;

	/* room for thunk sections without moving isecs */
	if (isecs.cap < isecs.n + 4096) {
		ld_input_section *nv = ld_region_alloc((size_t)(isecs.n + 4096) * sizeof(ld_input_section));
		memcpy(nv, isecs.v, isecs.n * sizeof(ld_input_section));
		isecs.v = nv;
		isecs.cap = isecs.n + 4096;
	}
	/* output sections in header order (live ones, script order) */
	ctx->layout.output_sections.n = 0;
	for (uint32_t c = 0; c < topcmds.n; c++)
		if (topcmds.v[c].kind == LD_COMMAND_OUTPUT_SECTION && osecs.v[topcmds.v[c].idx].live) {
			VVEC_PUSH(ctx->layout.output_sections, topcmds.v[c].idx, ld_region_alloc);
			osecs.v[topcmds.v[c].idx].shidx = ctx->layout.output_sections.n;
		}

	ld_addresses_assign();
	int pass = 0;
	for (;;) {
		bool changed = a64_ld_thunks_create(pass);
		pass++;
		if (changed && pass >= 30) {
			ld_error_report("address assignment did not converge");
			break;
		}
		bool chg2 = ld_addresses_assign();
		if (!changed && !chg2)
			break;
	}
	ctx->layout.in_layout = true;
	ld_addresses_assign();
	ctx->layout.in_layout = false;
	for (uint32_t i = 0; i < stop_syms.n; i++)
		gsyms[stop_syms.v[i]].ev.val = osecs.v[gsyms[stop_syms.v[i]].ev.idx].size;
	for (uint32_t i = 0; i < recorded.n; i++)
		ld_error_report("%s", recorded.v[i]);
	for (uint32_t i = 0; i < assert_fail.n; i++)
		ld_error_report("%s", assert_fail.v[i]);
	ld_phase_time_report("layout");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;

	/* drop empty thunk sections (none are kept by construction) */
	if (have_phdrs) {
		ld_script_program_headers_create();
	} else {
		ld_program_headers_create();
		ld_program_headers_finish();
	}
	/* symtab and shstrtab sizes are needed before file offsets */
	ld_section_name_table_build();
	ld_symbol_table_build();
	ld_dynamic_section_links_set();
	{
		ld_output_section *os = &osecs.v[ld_input_section_output_section(symtab_isec)];
		os->link = osecs.v[ld_input_section_output_section(strtab_isec)].shidx;
		os->info = nlocal_out;
		os->entsize = sizeof(elf_symbol);
	}
	/* sizes of the non-alloc synthetic sections changed: re-run layout of them */
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		ld_output_section *os = &osecs.v[ctx->layout.output_sections.v[i]];
		if (os->flags & SHF_ALLOC)
			continue;
		bool dummy = false;
		uint64_t d = ctx->layout.dot;
		ctx->layout.dot = 0;
		ld_output_section_offsets_assign(ctx->layout.output_sections.v[i], &dummy);
		ctx->layout.dot = d;
	}
	ld_file_offsets_assign();
	ld_phase_time_report("phdrs, symtab sizes, offsets");

	out = ld_region_large_alloc(ctx->layout.file_size);
	/* RELATIVE relocations: per section, in address order */
	if (reladyn_isec != LD_NONE && ndyn) {
		VVEC(uint32_t) dl = {0};
		for (uint32_t i = 0; i < isecs.n; i++)
			if (isecs.v[i].ndyn)
				VVEC_PUSH(dl, i, ld_region_alloc);
		qsort(dl.v, dl.n, sizeof(uint32_t), ld_dynamic_symbol_cmp);
		uint32_t at = 0;
		for (uint32_t i = 0; i < dl.n; i++) {
			isecs.v[dl.v[i]].dyn0 = at;
			at += isecs.v[dl.v[i]].ndyn;
		}
		/* built in place in the output image */
		reladyn_buf = (elf_rela *)(void *)(out + osecs.v[ld_input_section_output_section(reladyn_isec)].offset +
					       isecs.v[reladyn_isec].off);
	}
	wjobs.n = 0;
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != LD_COMMAND_OUTPUT_SECTION || !osecs.v[topcmds.v[c].idx].live)
			continue;
		ld_output_section *os = &osecs.v[topcmds.v[c].idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION) {
				ld_input_section_description *d = &isds.v[os->cmds.v[k].idx];
				for (uint32_t s = 0; s < d->secs.n; s++)
					VVEC_PUSH(wjobs, d->secs.v[s], ld_region_alloc);
			}
	}
	ld_final_addresses_compute();
	ld_gap_fills_write();
	ld_parallel_for(wjobs.n, ld_input_section_write, NULL);
	ld_dynamic_sections_write();
	ld_eh_frame_header_write();
	ld_phase_time_report("copy + relocate");
	ld_symbol_table_fill();
	ld_synthetic_sections_write();
	ld_headers_write();
	ld_phase_time_report("symtab, headers");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;
	ld_build_id_write();
	ld_phase_time_report("build id");
	int rc = ld_output_file_write();
	ld_phase_time_report("write");
	if (ctx->diagnostics.phase_timing)
		fprintf(ctx->diagnostics.error_stream, "  %-28s %7.2f ms (%u objects, %u sections, %u globals, %llu MB out, %d threads)\n",
			"total", (ld_clock_now() - t0) * 1e3, objs.n, isecs.n, gorder.n,
			(unsigned long long)(ctx->layout.file_size >> 20), ld_thread_count);
	ld_region_unmap(out, ctx->layout.file_size);
	for (uint32_t i = 0; i < objs.n; i++)
		if (objs.v[i]->mapped && objs.v[i]->buf)
			ld_region_unmap(objs.v[i]->buf, objs.v[i]->size);
	ld_global_table_free();
	return rc || atomic_load(&ctx->diagnostics.error_count) ? 1 : 0;
}
