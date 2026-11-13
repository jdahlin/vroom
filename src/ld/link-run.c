/* ld_link_run(): the passes of one ELF link, in order. */

#include "ld-internal.h"


VPRIV int ld_link_run(struct ld_context *ctx, int argc, char **argv)
{
	ld_thread_count = ld_parallel_default_width();
	ld_arguments_parse(ctx, argc, argv);
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;
	/* the standalone tool: the process is this link, so --threads is the budget
	   (inside vccd the server's budget stays, and nthreads only caps the width) */
	if (ld_standalone && ld_thread_count > vtask_budget())
		vtask_budget_set(ld_thread_count);
	double t0 = ld_clock_now();
	ctx->timing.start_time = t0;
	ctx->timing.start_counters = vperformance_now();
	ctx->timing.start_faults = ctx->timing.enabled ? ld_minor_fault_count() : 0;
	ctx->relocations.pic = ctx->options.shared;

	ld_inputs_load(ctx);
	ld_phase_time_report(ctx, "map+parse inputs");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;
	ld_global_table_init(ctx);
	if (ctx->options.script) {
		size_t n;
		uint8_t *b = ld_file_map(ctx, ctx->options.script, &n);
		if (!b)
			return 1;
		ld_script_parse(ctx, (const char *)b, n);
		if (atomic_load(&ctx->diagnostics.error_count))
			return 1;
	}
	ld_input_sections_alloc(ctx, 0);
	ld_parallel_for(ctx->inputs.objects.n, ld_input_sections_fill, ctx);
	ld_comdat_groups_deduplicate(ctx);
	ld_gnu_property_drop(ctx);
	ld_parallel_for(ctx->inputs.objects.n, ld_object_symbols_resolve, ctx);
	ld_parallel_for(ctx->inputs.objects.n, ld_duplicate_symbols_check, ctx);
	ld_phase_time_report(ctx, "resolve symbols");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;
	if (ctx->options.relocatable)
		return ld_relocatable_link(ctx);

	if (ctx->options.stubs) {
		ld_stubs_collect(ctx);
		if (ctx->symbols.stub_count) {
			ld_object *o = ld_stub_object_create(ctx, ctx->symbols.stub_names, ctx->symbols.stub_count);
			o->index = ctx->inputs.objects.n;
			VVEC_PUSH(ctx->inputs.objects, o, ld_region_alloc);
			ld_input_sections_alloc(ctx, o->index);
			ld_input_sections_fill(ctx, o->index);
			ld_object_symbols_resolve(ctx, o->index);
			ld_stub_list_write(ctx);
		}
	}
	ctx->inputs.real_object_count = ctx->inputs.objects.n;
	ld_script_symbols_declare(ctx);
	ld_global_order_build(ctx);
	ld_parallel_for(ctx->symbols.order.n, ld_global_symbol_finish_task, ctx);
	ld_symbol_versions_assign(ctx);
	ld_phase_time_report(ctx, "script symbols, order");

	if (ctx->options.garbage_collect_sections && !ctx->options.relocatable)
		ld_sections_garbage_collect(ctx, ctx->options.entry ? ctx->options.entry : ctx->script.entry ? ctx->script.entry : "_start");
	ld_common_symbols_replace(ctx);
	ld_synthetic_sections_add(ctx);
	ld_input_sections_assign(ctx);
	/* commit sections into their output sections, in order */
	for (uint32_t c = 0; c < ctx->script.commands.n; c++) {
		if (ctx->script.commands.v[c].kind != LD_COMMAND_OUTPUT_SECTION)
			continue;
		ld_output_section *os = &ctx->script.sections.v[ctx->script.commands.v[c].index];
		for (uint32_t k = 0; k < os->commands.n; k++)
			if (os->commands.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION) {
				ld_input_section_description *d = &ctx->script.input_descriptions.v[os->commands.v[k].index];
				for (uint32_t s = 0; s < d->sections.n; s++)
					ld_section_commit(ctx, os, &ctx->inputs.sections.v[d->sections.v[s]]);
			}
	}
	ld_dynamic_symbol_define(ctx);
	ld_start_stop_symbols_define(ctx);
	ld_parallel_for(ctx->symbols.order.n, ld_symbols_demote, ctx);
	ld_undefined_symbols_report(ctx);
	ld_phase_time_report(ctx, "assign sections");
	ld_merges_finalize(ctx);
	ld_phase_time_report(ctx, "merge sections");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;

	ld_symbol_assignments_process(ctx);
	ld_output_sections_adjust(ctx);
	if (ctx->output.got_section != LD_NONE)
		for (uint32_t i = 0; i < ctx->inputs.objects.n; i++)
			if (ctx->inputs.objects.v[i]->file_bytes && ctx->inputs.objects.v[i]->first_global_index)
				ctx->inputs.objects.v[i]->local_needs_got = calloc(ctx->inputs.objects.v[i]->first_global_index, 1);
	ld_parallel_for(ctx->inputs.sections.n, ctx->inputs.x86 ? x64_ld_relocation_scan : a64_ld_relocation_scan, ctx);
	/* .got: one slot per symbol, globals in symbol id order, then the locals by
	 * file (lld postScanRelocations) */
	for (uint32_t g = 0, ng = atomic_load(&ctx->symbols.count); g < ng; g++)
		if (atomic_load(&ctx->symbols.globals[g].needs_got)) {
			ctx->symbols.globals[g].got_index = ctx->output.got_symbols.n;
			VVEC_PUSH(ctx->output.got_symbols, g, ld_region_alloc);
			if (ctx->relocations.pic && ctx->output.got_section != LD_NONE && !ld_global_symbol_is_absolute(ctx, g) && ctx->symbols.globals[g].state != LD_SYMBOL_STATE_UNDEFINED && !ctx->symbols.globals[g].demoted)
				ctx->inputs.sections.v[ctx->output.got_section].dynamic_relocation_count++;
		}
	for (uint32_t i = 0; ctx->output.got_section != LD_NONE && i < ctx->inputs.objects.n; i++) {
		ld_object *o = ctx->inputs.objects.v[i];

		for (uint32_t k = 0; o->local_needs_got && k < o->first_global_index; k++)
			if (o->local_needs_got[k]) {
				if (!o->local_got_index)
					o->local_got_index = ld_region_alloc(o->first_global_index * sizeof(uint32_t));
				o->local_got_index[k] = ctx->output.got_symbols.n + ctx->output.got_locals.n;
				VVEC_PUSH(ctx->output.got_locals, ((ld_got_location){i, k}), ld_region_alloc);
				if (ctx->relocations.pic && o->symbols[k].st_shndx != SHN_ABS)
					ctx->inputs.sections.v[ctx->output.got_section].dynamic_relocation_count++;
			}
	}
	if (ctx->output.got_section != LD_NONE)
		ctx->inputs.sections.v[ctx->output.got_section].size = (uint64_t)(ctx->output.got_symbols.n + ctx->output.got_locals.n) * 8;
	ctx->output.relative_count = 0;
	for (uint32_t i = 0; i < ctx->inputs.sections.n; i++)
		ctx->output.relative_count += ctx->inputs.sections.v[i].dynamic_relocation_count;
	if (ctx->output.rela_dyn_section != LD_NONE)
		ctx->inputs.sections.v[ctx->output.rela_dyn_section].size = ctx->output.relative_count * sizeof(elf_rela);
	if (ctx->output.rela_dyn_section != LD_NONE && !ctx->output.relative_count && ld_input_section_is_live(ctx, ctx->output.rela_dyn_section)) {
		/* lld drops an empty .rela.dyn (RelocationSection::isNeeded) */
		ld_input_section_description *d = &ctx->script.input_descriptions.v[ctx->inputs.sections.v[ctx->output.rela_dyn_section].input_description];
		uint32_t w = 0;
		for (uint32_t k = 0; k < d->sections.n; k++)
			if (d->sections.v[k] != ctx->output.rela_dyn_section)
				d->sections.v[w++] = d->sections.v[k];
		d->sections.n = w;
		ctx->inputs.sections.v[ctx->output.rela_dyn_section].output_section = LD_NONE;
		ld_output_sections_adjust(ctx);
	}
	ld_dynamic_sections_build(ctx);
	ld_phase_time_report(ctx, "scan relocations");
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
		if (ctx->script.commands.v[c].kind == LD_COMMAND_OUTPUT_SECTION && ctx->script.sections.v[ctx->script.commands.v[c].index].live) {
			VVEC_PUSH(ctx->layout.output_sections, ctx->script.commands.v[c].index, ld_region_alloc);
			ctx->script.sections.v[ctx->script.commands.v[c].index].section_header_index = ctx->layout.output_sections.n;
		}

	ld_addresses_assign(ctx);
	int pass = 0;
	for (;;) {
		bool changed = a64_ld_thunks_create(ctx, pass);
		pass++;
		if (changed && pass >= 30) {
			ld_error_report(ctx, "address assignment did not converge");
			break;
		}
		bool chg2 = ld_addresses_assign(ctx);
		if (!changed && !chg2)
			break;
	}
	ctx->layout.in_layout = true;
	ld_addresses_assign(ctx);
	ctx->layout.in_layout = false;
	for (uint32_t i = 0; i < ctx->symbols.stop_symbols.n; i++)
		ctx->symbols.globals[ctx->symbols.stop_symbols.v[i]].expression_value.value = ctx->script.sections.v[ctx->symbols.globals[ctx->symbols.stop_symbols.v[i]].expression_value.index].size;
	for (uint32_t i = 0; i < ctx->script.address_errors.n; i++)
		ld_error_report(ctx, "%s", ctx->script.address_errors.v[i]);
	for (uint32_t i = 0; i < ctx->script.assert_failures.n; i++)
		ld_error_report(ctx, "%s", ctx->script.assert_failures.v[i]);
	ld_phase_time_report(ctx, "layout");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;

	/* drop empty thunk sections (none are kept by construction) */
	if (ctx->script.has_phdrs) {
		ld_script_program_headers_create(ctx);
	} else {
		ld_program_headers_create(ctx);
		ld_program_headers_finish(ctx);
	}
	/* symtab and shstrtab sizes are needed before file offsets */
	ld_section_name_table_build(ctx);
	ld_symbol_table_build(ctx);
	ld_dynamic_section_links_set(ctx);
	{
		ld_output_section *os = &ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.symtab_section)];
		os->link = ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.strtab_section)].section_header_index;
		os->info = ctx->symbol_table.local_count;
		os->entry_size = sizeof(elf_symbol);
	}
	/* sizes of the non-alloc synthetic sections changed: re-run layout of them */
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		ld_output_section *os = &ctx->script.sections.v[ctx->layout.output_sections.v[i]];
		if (os->flags & SHF_ALLOC)
			continue;
		bool dummy = false;
		uint64_t d = ctx->layout.dot;
		ctx->layout.dot = 0;
		ld_output_section_offsets_assign(ctx, ctx->layout.output_sections.v[i], &dummy);
		ctx->layout.dot = d;
	}
	ld_file_offsets_assign(ctx);
	ld_phase_time_report(ctx, "phdrs, symtab sizes, offsets");

	ctx->output.image = ld_region_large_alloc(ctx->layout.file_size);
	/* RELATIVE relocations: per section, in address order */
	if (ctx->output.rela_dyn_section != LD_NONE && ctx->output.relative_count) {
		VVEC(uint32_t) dl = {0};
		for (uint32_t i = 0; i < ctx->inputs.sections.n; i++)
			if (ctx->inputs.sections.v[i].dynamic_relocation_count)
				VVEC_PUSH(dl, i, ld_region_alloc);
		ld_section_ids_sort(ctx, dl.v, dl.n, LD_SECTION_ORDER_ADDRESS);
		uint32_t at = 0;
		for (uint32_t i = 0; i < dl.n; i++) {
			ctx->inputs.sections.v[dl.v[i]].first_dynamic_relocation = at;
			at += ctx->inputs.sections.v[dl.v[i]].dynamic_relocation_count;
		}
		/* built in place in the output image */
		ctx->relocations.rela_dyn_buffer = (elf_rela *)(void *)(ctx->output.image + ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.rela_dyn_section)].offset +
					       ctx->inputs.sections.v[ctx->output.rela_dyn_section].offset);
	}
	ctx->output.write_jobs.n = 0;
	for (uint32_t c = 0; c < ctx->script.commands.n; c++) {
		if (ctx->script.commands.v[c].kind != LD_COMMAND_OUTPUT_SECTION || !ctx->script.sections.v[ctx->script.commands.v[c].index].live)
			continue;
		ld_output_section *os = &ctx->script.sections.v[ctx->script.commands.v[c].index];
		for (uint32_t k = 0; k < os->commands.n; k++)
			if (os->commands.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION) {
				ld_input_section_description *d = &ctx->script.input_descriptions.v[os->commands.v[k].index];
				for (uint32_t s = 0; s < d->sections.n; s++)
					VVEC_PUSH(ctx->output.write_jobs, d->sections.v[s], ld_region_alloc);
			}
	}
	ld_final_addresses_compute(ctx);
	ld_gap_fills_write(ctx);
	ld_parallel_for(ctx->output.write_jobs.n, ld_input_section_write, ctx);
	ld_dynamic_sections_write(ctx);
	ld_eh_frame_header_write(ctx);
	ld_phase_time_report(ctx, "copy + relocate");
	ld_symbol_table_fill(ctx);
	ld_synthetic_sections_write(ctx);
	ld_headers_write(ctx);
	ld_phase_time_report(ctx, "symtab, headers");
	if (atomic_load(&ctx->diagnostics.error_count))
		return 1;
	ld_build_id_write(ctx);
	ld_phase_time_report(ctx, "build id");
	int rc = ld_output_file_write(ctx);
	ld_phase_time_report(ctx, "write");
	if (ctx->timing.enabled)
		fprintf(ctx->diagnostics.error_stream, "  %-28s %7.2f ms (%u objects, %u sections, %u globals, %llu MB out, %d threads)\n",
			"total", (ld_clock_now() - t0) * 1e3, ctx->inputs.objects.n, ctx->inputs.sections.n, ctx->symbols.order.n,
			(unsigned long long)(ctx->layout.file_size >> 20), ld_thread_count);
	ld_region_unmap(ctx->output.image, ctx->layout.file_size);
	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++)
		if (ctx->inputs.objects.v[i]->mapped && ctx->inputs.objects.v[i]->file_bytes)
			ld_region_unmap(ctx->inputs.objects.v[i]->file_bytes, ctx->inputs.objects.v[i]->size);
	ld_global_table_free(ctx);
	return rc || atomic_load(&ctx->diagnostics.error_count) ? 1 : 0;
}
