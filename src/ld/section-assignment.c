/* ld_input_sections_assign: matching input sections to the script's output sections, orphans, and the order within each. */

static void ld_input_sections_assign(void)
{
	ld_name_cache_init();
	for (uint32_t i = 0; i < isecs.n; i++) {
		ld_input_section *is = &isecs.v[i];
		if (is->kind == LD_SECTION_KIND_NONE)
			continue;
		uint32_t d = ld_input_section_description_match(is->name);
		if (d == LD_NONE) {
			VVEC_PUSH(ctx->layout.orphans, i, ld_region_alloc);
			continue;
		}
		ld_input_section_description *isd = &isds.v[d];
		if (osecs.v[isd->osec].discard) {
			is->osec = LD_OUTPUT_SECTION_DISCARD;
			continue;
		}
		is->isd = d;
		VVEC_PUSH(isd->secs, i, ld_region_alloc);
	}
	for (uint32_t i = 0; i < isds.n; i++) {
		ld_input_section_description *d = &isds.v[i];
		if (d->npats == 1 && d->pats[0].sort != LD_SORT_NONE)
			qsort(d->secs.v, d->secs.n, sizeof(uint32_t),
			      d->pats[0].sort == LD_SORT_NAME ? ld_section_name_stable_cmp : ld_section_alignment_stable_cmp);
		else
			for (uint32_t p = 0; p < d->npats; p++)
				if (d->pats[p].sort != LD_SORT_NONE)
					ld_error_report("linker script: SORT mixed with other patterns not supported");
	}
	for (uint32_t i = 0; i < ctx->layout.orphans.n; i++) {
		ld_input_section *is = &isecs.v[ctx->layout.orphans.v[i]];
		if (!strcmp(is->name, ".note.GNU-stack") || (is->flags & SHF_EXCLUDE))
			is->osec = LD_OUTPUT_SECTION_DISCARD;
		else if (!opt.orphan || is->kind == LD_SECTION_KIND_GOT)
			;	/* lld: placed silently by default */
		else
			(opt.orphan == 2 ? ld_error_report : ld_warning_report)("%s:(%s) is being placed in '%s'", is->obj != LD_NONE ? objs.v[is->obj]->path : "<internal>",
			     is->name, is->name);
	}
	/* lld sortSections: orphans stable-sorted by rank (non-allocated last), then
	 * placed one by one, each seeing those placed before it */
	{
		uint32_t n = ctx->layout.orphans.n, *tmp = ld_region_alloc(n * sizeof(uint32_t) + 1), k = 0;

		/* stable: by rank, non-allocated last */
		for (uint32_t i = 0; i < n; i++) {
			const ld_input_section *is = &isecs.v[ctx->layout.orphans.v[i]];
			uint64_t q = ld_orphan_section_key(is);
			uint32_t at = k++;

			while (at > 0) {
				const ld_input_section *p = &isecs.v[tmp[at - 1]];

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
		ld_input_section *is = &isecs.v[ctx->layout.orphans.v[i]];
		if (is->osec == LD_OUTPUT_SECTION_DISCARD)
			continue;
		uint32_t found = LD_NONE;
		for (uint32_t k = 0; k < osecs.n; k++)
			if (!strcmp(osecs.v[k].name, is->name))
				found = k;
		if (found == LD_NONE) {
			found = ld_output_section_find(is->name);
			VVEC_PUSH(topcmds, ((ld_command){.kind = LD_COMMAND_OUTPUT_SECTION, .idx = found}), ld_region_alloc);
			/* lld findOrphanPos, on the coarse ranks: the first output section of
			 * the closest rank (sections without input sections do not count),
			 * then past those as close whose rank is not above the orphan's */
			int r = ld_orphan_section_rank(is->flags, is->type, is->name);
			if (r >= 0) {
				uint32_t at = LD_NONE;
				int best = -1;
				for (uint32_t c = 0; c + 1 < topcmds.n; c++)
					if (topcmds.v[c].kind == LD_COMMAND_OUTPUT_SECTION) {
						int q = ld_output_section_rank(topcmds.v[c].idx);
						if (q >= 0 && ld_rank_proximity(q, r) > best) {
							best = ld_rank_proximity(q, r);
							at = c;
						}
					}
				if (at != LD_NONE) {
					uint32_t c = at;
					for (; c + 1 < topcmds.n; c++) {
						if (topcmds.v[c].kind != LD_COMMAND_OUTPUT_SECTION)
							continue;
						int q = ld_output_section_rank(topcmds.v[c].idx);
						if (q < 0)
							continue;
						if (ld_rank_proximity(q, r) != best || r < q)
							break;
					}
					/* lld: back to just past the last output section with input
					 * sections, then past symbol assignments other than '.' */
					if (c + 1 < topcmds.n) {
						uint32_t j = c;
						while (j > 0 && !(topcmds.v[j - 1].kind == LD_COMMAND_OUTPUT_SECTION && ld_output_section_rank(topcmds.v[j - 1].idx) >= 0))
							j--;
						if (j == 0)	/* none before: not ahead of the commands before the first section */
							while (j < c && topcmds.v[j].kind != LD_COMMAND_OUTPUT_SECTION)
								j++;
						while (j < c && topcmds.v[j].kind == LD_COMMAND_ASSIGNMENT && topcmds.v[j].as->sym != LD_NONE)
							j++;
						c = j;
					}
					/* lld: with no section with input sections after, the orphan goes at the very end */
					if (c + 1 < topcmds.n) {
						ld_command cm = topcmds.v[topcmds.n - 1];
						memmove(&topcmds.v[c + 1], &topcmds.v[c], (topcmds.n - 1 - c) * sizeof(ld_command));
						topcmds.v[c] = cm;
					}
				}
			}
		}
		ld_output_section *os = &osecs.v[found];
		uint32_t di = LD_NONE;
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION)
				di = os->cmds.v[k].idx;
		if (di == LD_NONE) {
			ld_input_section_description d = {.osec = found, .thunksec = LD_NONE};
			VVEC_PUSH(isds, d, ld_region_alloc);
			di = isds.n - 1;
			VVEC_PUSH(osecs.v[found].cmds, ((ld_command){.kind = LD_COMMAND_INPUT_SECTION_DESCRIPTION, .idx = di}), ld_region_alloc);
		}
		is->isd = di;
		VVEC_PUSH(isds.v[di].secs, ctx->layout.orphans.v[i], ld_region_alloc);
	}

	/* merge sections: one synthetic per (flags, entsize[, align]) per output section */
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != LD_COMMAND_OUTPUT_SECTION)
			continue;
		uint32_t oi = topcmds.v[c].idx;
		ld_output_section *os = &osecs.v[oi];
		uint32_t first_merge = merges.n;
		for (uint32_t k = 0; k < os->cmds.n; k++) {
			if (os->cmds.v[k].kind != LD_COMMAND_INPUT_SECTION_DESCRIPTION)
				continue;
			ld_input_section_description *d = &isds.v[os->cmds.v[k].idx];
			uint32_t w = 0;
			for (uint32_t s = 0; s < d->secs.n; s++) {
				uint32_t si = d->secs.v[s];
				ld_input_section *is = &isecs.v[si];
				if (is->kind != LD_SECTION_KIND_MERGE_INPUT) {
					d->secs.v[w++] = si;
					continue;
				}
				uint32_t m;
				for (m = first_merge; m < merges.n; m++) {
					ld_merge_section *mg = &merges.v[m];
					if (mg->flags == is->flags && mg->entsize == is->entsize &&
					    (mg->align == is->align || !(mg->flags & SHF_STRINGS)))
						break;
				}
				if (m == merges.n) {
					ld_merge_section mg = {.flags = is->flags, .entsize = is->entsize, .align = is->align,
						    .osec = oi};
					uint32_t syn = ld_input_section_create(isecs.v[si].name, LD_SECTION_KIND_MERGE, isecs.v[si].type,
								isecs.v[si].flags, isecs.v[si].align);
					is = &isecs.v[si];
					mg.isec = syn;
					isecs.v[syn].aux = m;
					isecs.v[syn].entsize = is->entsize;
					isecs.v[syn].isd = os->cmds.v[k].idx;
					VVEC_PUSH(merges, mg, ld_region_alloc);
					d->secs.v[w++] = syn;
				}
				ld_merge_section *mg = &merges.v[m];
				if (is->align > mg->align)
					mg->align = is->align;
				ld_merge_input mi = {.isec = si, .syn = mg->isec};
				VVEC_PUSH(mergeins, mi, ld_region_alloc);
				is->aux = mergeins.n - 1;
				VVEC_PUSH(mg->members, mergeins.n - 1, ld_region_alloc);
			}
			d->secs.n = w;
		}
		for (uint32_t m = first_merge; m < merges.n; m++)
			isecs.v[merges.v[m].isec].align = merges.v[m].align;
	}
	ld_eh_frame_merge();
	if (ctx->output.eh_frame_header_section != LD_NONE)	/* lld EhFrameHeader::getSize: 12 + 8 per FDE */
		isecs.v[ctx->output.eh_frame_header_section].size = 12 + 8 * (uint64_t)ctx->layout.eh_frame_fdes.n;
	/* output section ownership */
	for (uint32_t i = 0; i < isds.n; i++)
		for (uint32_t s = 0; s < isds.v[i].secs.n; s++)
			isecs.v[isds.v[i].secs.v[s]].osec = isds.v[i].osec;
}
