/* ld_input_sections_assign: matching input sections to the script's output sections, orphans, and the order within each. */

static void ld_input_sections_assign(struct ld_context *ctx)
{
	ld_name_cache_init(ctx);
	for (uint32_t i = 0; i < ctx->inputs.sections.n; i++) {
		ld_input_section *is = &ctx->inputs.sections.v[i];
		if (is->kind == LD_SECTION_KIND_NONE)
			continue;
		uint32_t d = ld_input_section_description_match(ctx, is->name);
		if (d == LD_NONE) {
			VVEC_PUSH(ctx->layout.orphans, i, ld_region_alloc);
			continue;
		}
		ld_input_section_description *isd = &ctx->script.input_descriptions.v[d];
		if (ctx->script.sections.v[isd->output_section].discard) {
			is->output_section = LD_OUTPUT_SECTION_DISCARD;
			continue;
		}
		is->input_description = d;
		VVEC_PUSH(isd->sections, i, ld_region_alloc);
	}
	for (uint32_t i = 0; i < ctx->script.input_descriptions.n; i++) {
		ld_input_section_description *d = &ctx->script.input_descriptions.v[i];
		if (d->pattern_count == 1 && d->patterns[0].sort != LD_SORT_NONE)
			ld_section_ids_sort(ctx, d->sections.v, d->sections.n,
					    d->patterns[0].sort == LD_SORT_NAME ? LD_SECTION_ORDER_NAME : LD_SECTION_ORDER_ALIGNMENT_DESCENDING);
		else
			for (uint32_t p = 0; p < d->pattern_count; p++)
				if (d->patterns[p].sort != LD_SORT_NONE)
					ld_error_report(ctx, "linker script: SORT mixed with other patterns not supported");
	}
	for (uint32_t i = 0; i < ctx->layout.orphans.n; i++) {
		ld_input_section *is = &ctx->inputs.sections.v[ctx->layout.orphans.v[i]];
		if (!strcmp(is->name, ".note.GNU-stack") || (is->flags & SHF_EXCLUDE))
			is->output_section = LD_OUTPUT_SECTION_DISCARD;
		else if (!ctx->options.orphan_handling || is->kind == LD_SECTION_KIND_GOT)
			;	/* lld: placed silently by default */
		else
			(ctx->options.orphan_handling == 2 ? ld_error_report : ld_warning_report)(ctx, "%s:(%s) is being placed in '%s'", is->object != LD_NONE ? ctx->inputs.objects.v[is->object]->path : "<internal>",
			     is->name, is->name);
	}
	/* lld sortSections: orphans stable-sorted by rank (non-allocated last), then
	 * placed one by one, each seeing those placed before it */
	{
		uint32_t n = ctx->layout.orphans.n, *tmp = ld_region_alloc(n * sizeof(uint32_t) + 1), k = 0;

		/* stable: by rank, non-allocated last */
		for (uint32_t i = 0; i < n; i++) {
			const ld_input_section *is = &ctx->inputs.sections.v[ctx->layout.orphans.v[i]];
			uint64_t q = ld_orphan_section_key(is);
			uint32_t at = k++;

			while (at > 0) {
				const ld_input_section *p = &ctx->inputs.sections.v[tmp[at - 1]];

				if (ld_orphan_section_key(p) <= q)
					break;
				tmp[at] = tmp[at - 1];
				at--;
			}
			tmp[at] = ctx->layout.orphans.v[i];
		}
		memcpy(ctx->layout.orphans.v, tmp, n * sizeof(uint32_t));
	}
	for (uint32_t i = 0; i < ctx->layout.orphans.n; i++) {
		ld_input_section *is = &ctx->inputs.sections.v[ctx->layout.orphans.v[i]];
		if (is->output_section == LD_OUTPUT_SECTION_DISCARD)
			continue;
		uint32_t found = LD_NONE;
		for (uint32_t k = 0; k < ctx->script.sections.n; k++)
			if (!strcmp(ctx->script.sections.v[k].name, is->name))
				found = k;
		if (found == LD_NONE) {
			found = ld_output_section_find(ctx, is->name);
			VVEC_PUSH(ctx->script.commands, ((ld_command){.kind = LD_COMMAND_OUTPUT_SECTION, .index = found}), ld_region_alloc);
			/* lld findOrphanPos, on the coarse ranks: the first output section of
			 * the closest rank (sections without input sections do not count),
			 * then past those as close whose rank is not above the orphan's */
			int r = ld_orphan_section_rank(is->flags, is->type, is->name);
			if (r >= 0) {
				uint32_t at = LD_NONE;
				int best = -1;
				for (uint32_t c = 0; c + 1 < ctx->script.commands.n; c++)
					if (ctx->script.commands.v[c].kind == LD_COMMAND_OUTPUT_SECTION) {
						int q = ld_output_section_rank(ctx, ctx->script.commands.v[c].index);
						if (q >= 0 && ld_rank_proximity(q, r) > best) {
							best = ld_rank_proximity(q, r);
							at = c;
						}
					}
				if (at != LD_NONE) {
					uint32_t c = at;
					for (; c + 1 < ctx->script.commands.n; c++) {
						if (ctx->script.commands.v[c].kind != LD_COMMAND_OUTPUT_SECTION)
							continue;
						int q = ld_output_section_rank(ctx, ctx->script.commands.v[c].index);
						if (q < 0)
							continue;
						if (ld_rank_proximity(q, r) != best || r < q)
							break;
					}
					/* lld: back to just past the last output section with input
					 * sections, then past symbol assignments other than '.' */
					if (c + 1 < ctx->script.commands.n) {
						uint32_t j = c;
						while (j > 0 && !(ctx->script.commands.v[j - 1].kind == LD_COMMAND_OUTPUT_SECTION && ld_output_section_rank(ctx, ctx->script.commands.v[j - 1].index) >= 0))
							j--;
						if (j == 0)	/* none before: not ahead of the commands before the first section */
							while (j < c && ctx->script.commands.v[j].kind != LD_COMMAND_OUTPUT_SECTION)
								j++;
						while (j < c && ctx->script.commands.v[j].kind == LD_COMMAND_ASSIGNMENT && ctx->script.commands.v[j].assignment->symbol != LD_NONE)
							j++;
						c = j;
					}
					/* lld: with no section with input sections after, the orphan goes at the very end */
					if (c + 1 < ctx->script.commands.n) {
						ld_command cm = ctx->script.commands.v[ctx->script.commands.n - 1];
						memmove(&ctx->script.commands.v[c + 1], &ctx->script.commands.v[c], (ctx->script.commands.n - 1 - c) * sizeof(ld_command));
						ctx->script.commands.v[c] = cm;
					}
				}
			}
		}
		ld_output_section *os = &ctx->script.sections.v[found];
		uint32_t di = LD_NONE;
		for (uint32_t k = 0; k < os->commands.n; k++)
			if (os->commands.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION)
				di = os->commands.v[k].index;
		if (di == LD_NONE) {
			ld_input_section_description d = {.output_section = found, .thunk_section = LD_NONE};
			VVEC_PUSH(ctx->script.input_descriptions, d, ld_region_alloc);
			di = ctx->script.input_descriptions.n - 1;
			VVEC_PUSH(ctx->script.sections.v[found].commands, ((ld_command){.kind = LD_COMMAND_INPUT_SECTION_DESCRIPTION, .index = di}), ld_region_alloc);
		}
		is->input_description = di;
		VVEC_PUSH(ctx->script.input_descriptions.v[di].sections, ctx->layout.orphans.v[i], ld_region_alloc);
	}

	/* merge sections: one synthetic per (flags, entsize[, align]) per output section */
	for (uint32_t c = 0; c < ctx->script.commands.n; c++) {
		if (ctx->script.commands.v[c].kind != LD_COMMAND_OUTPUT_SECTION)
			continue;
		uint32_t oi = ctx->script.commands.v[c].index;
		ld_output_section *os = &ctx->script.sections.v[oi];
		uint32_t first_merge = ctx->inputs.merge_sections.n;
		for (uint32_t k = 0; k < os->commands.n; k++) {
			if (os->commands.v[k].kind != LD_COMMAND_INPUT_SECTION_DESCRIPTION)
				continue;
			ld_input_section_description *d = &ctx->script.input_descriptions.v[os->commands.v[k].index];
			uint32_t w = 0;
			for (uint32_t s = 0; s < d->sections.n; s++) {
				uint32_t si = d->sections.v[s];
				ld_input_section *is = &ctx->inputs.sections.v[si];
				if (is->kind != LD_SECTION_KIND_MERGE_INPUT) {
					d->sections.v[w++] = si;
					continue;
				}
				uint32_t m;
				for (m = first_merge; m < ctx->inputs.merge_sections.n; m++) {
					ld_merge_section *mg = &ctx->inputs.merge_sections.v[m];
					if (mg->flags == is->flags && mg->entry_size == is->entry_size &&
					    (mg->alignment == is->alignment || !(mg->flags & SHF_STRINGS)))
						break;
				}
				if (m == ctx->inputs.merge_sections.n) {
					ld_merge_section mg = {.flags = is->flags, .entry_size = is->entry_size, .alignment = is->alignment,
						    .output_section = oi};
					uint32_t syn = ld_input_section_create(ctx, ctx->inputs.sections.v[si].name, LD_SECTION_KIND_MERGE, ctx->inputs.sections.v[si].type,
								ctx->inputs.sections.v[si].flags, ctx->inputs.sections.v[si].alignment);
					is = &ctx->inputs.sections.v[si];
					mg.synthetic_section = syn;
					ctx->inputs.sections.v[syn].auxiliary_index = m;
					ctx->inputs.sections.v[syn].entry_size = is->entry_size;
					ctx->inputs.sections.v[syn].input_description = os->commands.v[k].index;
					VVEC_PUSH(ctx->inputs.merge_sections, mg, ld_region_alloc);
					d->sections.v[w++] = syn;
				}
				ld_merge_section *mg = &ctx->inputs.merge_sections.v[m];
				if (is->alignment > mg->alignment)
					mg->alignment = is->alignment;
				ld_merge_input mi = {.input_section = si, .synthetic_section = mg->synthetic_section};
				VVEC_PUSH(ctx->inputs.merge_inputs, mi, ld_region_alloc);
				is->auxiliary_index = ctx->inputs.merge_inputs.n - 1;
				VVEC_PUSH(mg->members, ctx->inputs.merge_inputs.n - 1, ld_region_alloc);
			}
			d->sections.n = w;
		}
		for (uint32_t m = first_merge; m < ctx->inputs.merge_sections.n; m++)
			ctx->inputs.sections.v[ctx->inputs.merge_sections.v[m].synthetic_section].alignment = ctx->inputs.merge_sections.v[m].alignment;
	}
	ld_eh_frame_merge(ctx);
	if (ctx->output.eh_frame_header_section != LD_NONE)	/* lld EhFrameHeader::getSize: 12 + 8 per FDE */
		ctx->inputs.sections.v[ctx->output.eh_frame_header_section].size = 12 + 8 * (uint64_t)ctx->layout.eh_frame_fdes.n;
	/* output section ownership */
	for (uint32_t i = 0; i < ctx->script.input_descriptions.n; i++)
		for (uint32_t s = 0; s < ctx->script.input_descriptions.v[i].sections.n; s++)
			ctx->inputs.sections.v[ctx->script.input_descriptions.v[i].sections.v[s]].output_section = ctx->script.input_descriptions.v[i].output_section;
}
