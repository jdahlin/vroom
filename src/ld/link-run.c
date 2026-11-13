/* ld_link_run(): the passes of one ELF link, in order. */

static int ld_relocatable_link(void);

static int ld_link_run(int argc, char **argv)
{
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
	ctx->relocations.pic = ctx->options.shared;

	ld_inputs_load();
	ld_phase_time_report("map+parse inputs");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;
	ld_global_table_init();
	if (ctx->options.script) {
		size_t n;
		uint8_t *b = ld_file_map(ctx->options.script, &n);
		if (!b)
			return 1;
		ld_script_parse((const char *)b, n);
		if (atomic_load(&ctx->diagnostics.error_count))
			return 1;
	}
	ld_input_sections_alloc(0);
	ld_parallel_for(ctx->inputs.objects.n, ld_input_sections_fill, NULL);
	ld_comdat_groups_deduplicate();
	ld_gnu_property_drop();
	ld_parallel_for(ctx->inputs.objects.n, ld_object_symbols_resolve, NULL);
	ld_parallel_for(ctx->inputs.objects.n, ld_duplicate_symbols_check, NULL);
	ld_phase_time_report("resolve symbols");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;
	if (ctx->options.relocatable)
		return ld_relocatable_link();

	if (ctx->options.stubs) {
		ld_stubs_collect();
		if (ctx->symbols.stub_count) {
			ld_object *o = ld_stub_object_create(ctx->symbols.stub_names, ctx->symbols.stub_count);
			o->idx = ctx->inputs.objects.n;
			VVEC_PUSH(ctx->inputs.objects, o, ld_region_alloc);
			ld_input_sections_alloc(o->idx);
			ld_input_sections_fill(NULL, o->idx);
			ld_object_symbols_resolve(NULL, o->idx);
			ld_stub_list_write();
		}
	}
	ctx->inputs.real_object_count = ctx->inputs.objects.n;
	ld_script_symbols_declare();
	ld_global_order_build();
	ld_parallel_for(ctx->symbols.order.n, ld_global_symbol_finish_task, NULL);
	ld_symbol_versions_assign();
	ld_phase_time_report("script symbols, order");

	if (ctx->options.gc && !ctx->options.relocatable)
		ld_sections_garbage_collect(ctx->options.entry ? ctx->options.entry : ctx->script.entry ? ctx->script.entry : "_start");
	ld_common_symbols_replace();
	ld_synthetic_sections_add();
	ld_input_sections_assign();
	/* commit sections into their output sections, in order */
	for (uint32_t c = 0; c < ctx->script.commands.n; c++) {
		if (ctx->script.commands.v[c].kind != LD_COMMAND_OUTPUT_SECTION)
			continue;
		ld_output_section *os = &ctx->script.sections.v[ctx->script.commands.v[c].idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION) {
				ld_input_section_description *d = &ctx->script.input_descriptions.v[os->cmds.v[k].idx];
				for (uint32_t s = 0; s < d->secs.n; s++)
					ld_section_commit(os, &ctx->inputs.sections.v[d->secs.v[s]]);
			}
	}
	ld_dynamic_symbol_define();
	ld_start_stop_symbols_define();
	ld_parallel_for(ctx->symbols.order.n, ld_symbols_demote, NULL);
	ld_undefined_symbols_report();
	ld_phase_time_report("assign sections");
	ld_merges_finalize();
	ld_phase_time_report("merge sections");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;

	ld_symbol_assignments_process();
	ld_output_sections_adjust();
	if (ctx->output.got_section != LD_NONE)
		for (uint32_t i = 0; i < ctx->inputs.objects.n; i++)
			if (ctx->inputs.objects.v[i]->buf && ctx->inputs.objects.v[i]->firstg)
				ctx->inputs.objects.v[i]->lgot = calloc(ctx->inputs.objects.v[i]->firstg, 1);
	ld_parallel_for(ctx->inputs.sections.n, ctx->inputs.x86 ? x64_ld_relocation_scan : a64_ld_relocation_scan, NULL);
	/* .got: one slot per symbol, globals in symbol id order, then the locals by
	 * file (lld postScanRelocations) */
	for (uint32_t g = 0, ng = atomic_load(&ctx->symbols.count); g < ng; g++)
		if (atomic_load(&ctx->symbols.globals[g].needgot)) {
			ctx->symbols.globals[g].gotidx = ctx->output.got_symbols.n;
			VVEC_PUSH(ctx->output.got_symbols, g, ld_region_alloc);
			if (ctx->relocations.pic && ctx->output.got_section != LD_NONE && !ld_global_symbol_is_absolute(g) && ctx->symbols.globals[g].state != LD_SYMBOL_STATE_UNDEFINED && !ctx->symbols.globals[g].demoted)
				ctx->inputs.sections.v[ctx->output.got_section].ndyn++;
		}
	for (uint32_t i = 0; ctx->output.got_section != LD_NONE && i < ctx->inputs.objects.n; i++) {
		ld_object *o = ctx->inputs.objects.v[i];

		for (uint32_t k = 0; o->lgot && k < o->firstg; k++)
			if (o->lgot[k]) {
				if (!o->lgotidx)
					o->lgotidx = ld_region_alloc(o->firstg * sizeof(uint32_t));
				o->lgotidx[k] = ctx->output.got_symbols.n + ctx->output.got_locals.n;
				VVEC_PUSH(ctx->output.got_locals, ((ld_got_location){i, k}), ld_region_alloc);
				if (ctx->relocations.pic && o->sym[k].st_shndx != SHN_ABS)
					ctx->inputs.sections.v[ctx->output.got_section].ndyn++;
			}
	}
	if (ctx->output.got_section != LD_NONE)
		ctx->inputs.sections.v[ctx->output.got_section].size = (uint64_t)(ctx->output.got_symbols.n + ctx->output.got_locals.n) * 8;
	ctx->output.relative_count = 0;
	for (uint32_t i = 0; i < ctx->inputs.sections.n; i++)
		ctx->output.relative_count += ctx->inputs.sections.v[i].ndyn;
	if (ctx->output.rela_dyn_section != LD_NONE)
		ctx->inputs.sections.v[ctx->output.rela_dyn_section].size = ctx->output.relative_count * sizeof(elf_rela);
	if (ctx->output.rela_dyn_section != LD_NONE && !ctx->output.relative_count && ld_input_section_is_live(ctx->output.rela_dyn_section)) {
		/* lld drops an empty .rela.dyn (RelocationSection::isNeeded) */
		ld_input_section_description *d = &ctx->script.input_descriptions.v[ctx->inputs.sections.v[ctx->output.rela_dyn_section].isd];
		uint32_t w = 0;
		for (uint32_t k = 0; k < d->secs.n; k++)
			if (d->secs.v[k] != ctx->output.rela_dyn_section)
				d->secs.v[w++] = d->secs.v[k];
		d->secs.n = w;
		ctx->inputs.sections.v[ctx->output.rela_dyn_section].osec = LD_NONE;
		ld_output_sections_adjust();
	}
	ld_dynamic_sections_build();
	ld_phase_time_report("scan relocations");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;

	/* room for thunk sections without moving isecs */
	if (ctx->inputs.sections.cap < ctx->inputs.sections.n + 4096) {
		ld_input_section *nv = ld_region_alloc((size_t)(ctx->inputs.sections.n + 4096) * sizeof(ld_input_section));
		memcpy(nv, ctx->inputs.sections.v, ctx->inputs.sections.n * sizeof(ld_input_section));
		ctx->inputs.sections.v = nv;
		ctx->inputs.sections.cap = ctx->inputs.sections.n + 4096;
	}
	/* output sections in header order (live ones, script order) */
	ctx->layout.output_sections.n = 0;
	for (uint32_t c = 0; c < ctx->script.commands.n; c++)
		if (ctx->script.commands.v[c].kind == LD_COMMAND_OUTPUT_SECTION && ctx->script.sections.v[ctx->script.commands.v[c].idx].live) {
			VVEC_PUSH(ctx->layout.output_sections, ctx->script.commands.v[c].idx, ld_region_alloc);
			ctx->script.sections.v[ctx->script.commands.v[c].idx].shidx = ctx->layout.output_sections.n;
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
	for (uint32_t i = 0; i < ctx->symbols.stop_symbols.n; i++)
		ctx->symbols.globals[ctx->symbols.stop_symbols.v[i]].ev.val = ctx->script.sections.v[ctx->symbols.globals[ctx->symbols.stop_symbols.v[i]].ev.idx].size;
	for (uint32_t i = 0; i < ctx->script.address_errors.n; i++)
		ld_error_report("%s", ctx->script.address_errors.v[i]);
	for (uint32_t i = 0; i < ctx->script.assert_failures.n; i++)
		ld_error_report("%s", ctx->script.assert_failures.v[i]);
	ld_phase_time_report("layout");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;

	/* drop empty thunk sections (none are kept by construction) */
	if (ctx->script.has_phdrs) {
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
		ld_output_section *os = &ctx->script.sections.v[ld_input_section_output_section(ctx->output.symtab_section)];
		os->link = ctx->script.sections.v[ld_input_section_output_section(ctx->output.strtab_section)].shidx;
		os->info = ctx->symbol_table.local_count;
		os->entsize = sizeof(elf_symbol);
	}
	/* sizes of the non-alloc synthetic sections changed: re-run layout of them */
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		ld_output_section *os = &ctx->script.sections.v[ctx->layout.output_sections.v[i]];
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

	ctx->output.image = ld_region_large_alloc(ctx->layout.file_size);
	/* RELATIVE relocations: per section, in address order */
	if (ctx->output.rela_dyn_section != LD_NONE && ctx->output.relative_count) {
		VVEC(uint32_t) dl = {0};
		for (uint32_t i = 0; i < ctx->inputs.sections.n; i++)
			if (ctx->inputs.sections.v[i].ndyn)
				VVEC_PUSH(dl, i, ld_region_alloc);
		qsort(dl.v, dl.n, sizeof(uint32_t), ld_dynamic_symbol_cmp);
		uint32_t at = 0;
		for (uint32_t i = 0; i < dl.n; i++) {
			ctx->inputs.sections.v[dl.v[i]].dyn0 = at;
			at += ctx->inputs.sections.v[dl.v[i]].ndyn;
		}
		/* built in place in the output image */
		ctx->relocations.rela_dyn_buffer = (elf_rela *)(void *)(ctx->output.image + ctx->script.sections.v[ld_input_section_output_section(ctx->output.rela_dyn_section)].offset +
					       ctx->inputs.sections.v[ctx->output.rela_dyn_section].off);
	}
	ctx->output.write_jobs.n = 0;
	for (uint32_t c = 0; c < ctx->script.commands.n; c++) {
		if (ctx->script.commands.v[c].kind != LD_COMMAND_OUTPUT_SECTION || !ctx->script.sections.v[ctx->script.commands.v[c].idx].live)
			continue;
		ld_output_section *os = &ctx->script.sections.v[ctx->script.commands.v[c].idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION) {
				ld_input_section_description *d = &ctx->script.input_descriptions.v[os->cmds.v[k].idx];
				for (uint32_t s = 0; s < d->secs.n; s++)
					VVEC_PUSH(ctx->output.write_jobs, d->secs.v[s], ld_region_alloc);
			}
	}
	ld_final_addresses_compute();
	ld_gap_fills_write();
	ld_parallel_for(ctx->output.write_jobs.n, ld_input_section_write, NULL);
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
			"total", (ld_clock_now() - t0) * 1e3, ctx->inputs.objects.n, ctx->inputs.sections.n, ctx->symbols.order.n,
			(unsigned long long)(ctx->layout.file_size >> 20), ld_thread_count);
	ld_region_unmap(ctx->output.image, ctx->layout.file_size);
	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++)
		if (ctx->inputs.objects.v[i]->mapped && ctx->inputs.objects.v[i]->buf)
			ld_region_unmap(ctx->inputs.objects.v[i]->buf, ctx->inputs.objects.v[i]->size);
	ld_global_table_free();
	return rc || atomic_load(&ctx->diagnostics.error_count) ? 1 : 0;
}
