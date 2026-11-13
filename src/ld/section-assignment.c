/* assign_sections: matching input sections to the script's output sections, orphans, and the order within each. */

static void assign_sections(void)
{
	ncache_init();
	for (uint32_t i = 0; i < isecs.n; i++) {
		ISec *is = &isecs.v[i];
		if (is->kind == K_NONE)
			continue;
		uint32_t d = match_isd(is->name);
		if (d == NONE) {
			VVEC_PUSH(orphans, i, alloc);
			continue;
		}
		ISD *isd = &isds.v[d];
		if (osecs.v[isd->osec].discard) {
			is->osec = OS_DISCARD;
			continue;
		}
		is->isd = d;
		VVEC_PUSH(isd->secs, i, alloc);
	}
	for (uint32_t i = 0; i < isds.n; i++) {
		ISD *d = &isds.v[i];
		if (d->npats == 1 && d->pats[0].sort != SORT_NONE)
			qsort(d->secs.v, d->secs.n, sizeof(uint32_t),
			      d->pats[0].sort == SORT_NAME ? cmp_name_stable : cmp_align_stable);
		else
			for (uint32_t p = 0; p < d->npats; p++)
				if (d->pats[p].sort != SORT_NONE)
					error("linker script: SORT mixed with other patterns not supported");
	}
	for (uint32_t i = 0; i < orphans.n; i++) {
		ISec *is = &isecs.v[orphans.v[i]];
		if (!strcmp(is->name, ".note.GNU-stack") || (is->flags & SHF_EXCLUDE))
			is->osec = OS_DISCARD;
		else if (!opt.orphan || is->kind == K_GOT)
			;	/* lld: placed silently by default */
		else
			(opt.orphan == 2 ? error : warn)("%s:(%s) is being placed in '%s'", is->obj != NONE ? objs.v[is->obj]->path : "<internal>",
			     is->name, is->name);
	}
	/* lld sortSections: orphans stable-sorted by rank (non-allocated last), then
	 * placed one by one, each seeing those placed before it */
	{
		uint32_t n = orphans.n, *tmp = alloc(n * sizeof(uint32_t) + 1), k = 0;

		/* stable: by rank, non-allocated last */
		for (uint32_t i = 0; i < n; i++) {
			const ISec *is = &isecs.v[orphans.v[i]];
			uint64_t q = orphan_key(is);
			uint32_t at = k++;

			while (at > 0) {
				const ISec *p = &isecs.v[tmp[at - 1]];

				if (orphan_key(p) <= q)
					break;
				tmp[at] = tmp[at - 1];
				at--;
			}
			tmp[at] = orphans.v[i];
		}
		memcpy(orphans.v, tmp, n * sizeof(uint32_t));
	}
	for (uint32_t i = 0; i < orphans.n; i++) {
		ISec *is = &isecs.v[orphans.v[i]];
		if (is->osec == OS_DISCARD)
			continue;
		uint32_t found = NONE;
		for (uint32_t k = 0; k < osecs.n; k++)
			if (!strcmp(osecs.v[k].name, is->name))
				found = k;
		if (found == NONE) {
			found = get_osec(is->name);
			VVEC_PUSH(topcmds, ((Cmd){.kind = C_OSEC, .idx = found}), alloc);
			/* lld findOrphanPos, on the coarse ranks: the first output section of
			 * the closest rank (sections without input sections do not count),
			 * then past those as close whose rank is not above the orphan's */
			int r = orphan_rank(is->flags, is->type, is->name);
			if (r >= 0) {
				uint32_t at = NONE;
				int best = -1;
				for (uint32_t c = 0; c + 1 < topcmds.n; c++)
					if (topcmds.v[c].kind == C_OSEC) {
						int q = osec_rank(topcmds.v[c].idx);
						if (q >= 0 && rank_prox(q, r) > best) {
							best = rank_prox(q, r);
							at = c;
						}
					}
				if (at != NONE) {
					uint32_t c = at;
					for (; c + 1 < topcmds.n; c++) {
						if (topcmds.v[c].kind != C_OSEC)
							continue;
						int q = osec_rank(topcmds.v[c].idx);
						if (q < 0)
							continue;
						if (rank_prox(q, r) != best || r < q)
							break;
					}
					/* lld: back to just past the last output section with input
					 * sections, then past symbol assignments other than '.' */
					if (c + 1 < topcmds.n) {
						uint32_t j = c;
						while (j > 0 && !(topcmds.v[j - 1].kind == C_OSEC && osec_rank(topcmds.v[j - 1].idx) >= 0))
							j--;
						if (j == 0)	/* none before: not ahead of the commands before the first section */
							while (j < c && topcmds.v[j].kind != C_OSEC)
								j++;
						while (j < c && topcmds.v[j].kind == C_ASSIGN && topcmds.v[j].as->sym != NONE)
							j++;
						c = j;
					}
					/* lld: with no section with input sections after, the orphan goes at the very end */
					if (c + 1 < topcmds.n) {
						Cmd cm = topcmds.v[topcmds.n - 1];
						memmove(&topcmds.v[c + 1], &topcmds.v[c], (topcmds.n - 1 - c) * sizeof(Cmd));
						topcmds.v[c] = cm;
					}
				}
			}
		}
		OSec *os = &osecs.v[found];
		uint32_t di = NONE;
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == C_ISD)
				di = os->cmds.v[k].idx;
		if (di == NONE) {
			ISD d = {.osec = found, .thunksec = NONE};
			VVEC_PUSH(isds, d, alloc);
			di = isds.n - 1;
			VVEC_PUSH(osecs.v[found].cmds, ((Cmd){.kind = C_ISD, .idx = di}), alloc);
		}
		is->isd = di;
		VVEC_PUSH(isds.v[di].secs, orphans.v[i], alloc);
	}

	/* merge sections: one synthetic per (flags, entsize[, align]) per output section */
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != C_OSEC)
			continue;
		uint32_t oi = topcmds.v[c].idx;
		OSec *os = &osecs.v[oi];
		uint32_t first_merge = merges.n;
		for (uint32_t k = 0; k < os->cmds.n; k++) {
			if (os->cmds.v[k].kind != C_ISD)
				continue;
			ISD *d = &isds.v[os->cmds.v[k].idx];
			uint32_t w = 0;
			for (uint32_t s = 0; s < d->secs.n; s++) {
				uint32_t si = d->secs.v[s];
				ISec *is = &isecs.v[si];
				if (is->kind != K_MERGE_IN) {
					d->secs.v[w++] = si;
					continue;
				}
				uint32_t m;
				for (m = first_merge; m < merges.n; m++) {
					Merge *mg = &merges.v[m];
					if (mg->flags == is->flags && mg->entsize == is->entsize &&
					    (mg->align == is->align || !(mg->flags & SHF_STRINGS)))
						break;
				}
				if (m == merges.n) {
					Merge mg = {.flags = is->flags, .entsize = is->entsize, .align = is->align,
						    .osec = oi};
					uint32_t syn = new_isec(isecs.v[si].name, K_MERGE, isecs.v[si].type,
								isecs.v[si].flags, isecs.v[si].align);
					is = &isecs.v[si];
					mg.isec = syn;
					isecs.v[syn].aux = m;
					isecs.v[syn].entsize = is->entsize;
					isecs.v[syn].isd = os->cmds.v[k].idx;
					VVEC_PUSH(merges, mg, alloc);
					d->secs.v[w++] = syn;
				}
				Merge *mg = &merges.v[m];
				if (is->align > mg->align)
					mg->align = is->align;
				MergeIn mi = {.isec = si, .syn = mg->isec};
				VVEC_PUSH(mergeins, mi, alloc);
				is->aux = mergeins.n - 1;
				VVEC_PUSH(mg->members, mergeins.n - 1, alloc);
			}
			d->secs.n = w;
		}
		for (uint32_t m = first_merge; m < merges.n; m++)
			isecs.v[merges.v[m].isec].align = merges.v[m].align;
	}
	eh_frames();
	if (ehhdr_isec != NONE)	/* lld EhFrameHeader::getSize: 12 + 8 per FDE */
		isecs.v[ehhdr_isec].size = 12 + 8 * (uint64_t)ehfdes.n;
	/* output section ownership */
	for (uint32_t i = 0; i < isds.n; i++)
		for (uint32_t s = 0; s < isds.v[i].secs.n; s++)
			isecs.v[isds.v[i].secs.v[s]].osec = isds.v[i].osec;
}
