/* Copying each input section into the output image and applying its relocations, in parallel. */


static void ld_input_section_write(void *arg, size_t j)
{
	(void)arg;
	uint32_t si = ctx->output.write_jobs.v[j];
	ld_input_section *is = &isecs.v[si];
	uint32_t oi = ld_input_section_output_section(si);
	ld_output_section *os = &osecs.v[oi];
	if (os->type == SHT_NOBITS || is->type == SHT_NOBITS)
		return;
	uint8_t *dst = ctx->output.image + os->offset + is->off;
	switch (is->kind) {
	case LD_SECTION_KIND_NORMAL:
		memcpy(dst, is->data, is->size);
		if (is->ehcie)	/* an FDE's CIE pointer: from the field to its CIE */
			ld_u32_store(dst + 4, (uint32_t)(ld_input_section_address(si, 4) - ld_input_section_address(is->ehcie - 1, 0)));
		if (is->nrela && (is->flags & SHF_ALLOC || !opt.relocatable)) {
			if (x86_ld)
				x64_ld_input_section_relocate(si, dst);
			else
				a64_ld_input_section_relocate(si, dst);
		}
		break;
	case LD_SECTION_KIND_MERGE: {
		ld_merge_section *mg = &merges.v[is->aux];
		for (int s = 0; s < 32; s++)
			for (uint32_t k = 0; k < mg->uniq[s].n; k++) {
				uint64_t u = mg->uniq[s].v[k];
				ld_merge_input *m = &mergeins.v[u >> 32];
				uint32_t p = (uint32_t)u;
				memcpy(dst + m->outoff[p], isecs.v[m->isec].data + m->inoff[p], ld_merge_piece_length(m, p));
			}
		break;
	}
	case LD_SECTION_KIND_THUNK: {
		a64_ld_thunk_section *ts = &ctx->relocations.thunk_sections.v[is->aux];
		for (uint32_t k = 0; k < ts->list.n; k++) {
			uint32_t t = ts->list.v[k];
			uint8_t *b = dst + ctx->relocations.thunks.v[t].off;
			uint64_t s = a64_ld_thunk_destination(t), p = a64_ld_thunk_address(t);
			if (!ctx->relocations.thunks.v[t].lng) {
				ld_u32_store(b, 0x14000000 | (uint32_t)(((s - p) & 0x0ffffffc) >> 2));
				continue;
			}
			int64_t pg = (int64_t)((s & ~0xfffULL) - (p & ~0xfffULL));
			uint64_t imm = (uint64_t)pg >> 12;
			uint32_t adrp = 0x90000010 | (uint32_t)((imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5);
			ld_u32_store(b, adrp);
			ld_u32_store(b + 4, 0x91000210 | (uint32_t)(s & 0xfff) << 10);
			ld_u32_store(b + 8, 0xd61f0200);
		}
		break;
	}
	case LD_SECTION_KIND_GOT: {
		elf_rela *dyn = ctx->relocations.rela_dyn_buffer ? ctx->relocations.rela_dyn_buffer + is->dyn0 : NULL;
		uint32_t nd = 0;
		for (uint32_t k = 0; k < ctx->output.got_symbols.n; k++) {
			uint32_t g = ctx->output.got_symbols.v[k];
			bool undef = gsyms[g].state == LD_SYMBOL_STATE_UNDEFINED || gsyms[g].demoted;
			uint64_t v = undef ? 0 : ld_global_symbol_address(g);
			ld_u64_store(dst + 8 * k, v);
			if (ctx->relocations.pic && !undef && !ld_global_symbol_is_absolute(g)) {
				dyn[nd].r_offset = ld_input_section_address(si, 8 * k);
				dyn[nd].r_info = R_AARCH64_RELATIVE;
				dyn[nd].r_addend = (int64_t)v;
				nd++;
			}
		}
		for (uint32_t k = 0; k < ctx->output.got_locals.n; k++) {
			const ld_object *o = objs.v[ctx->output.got_locals.v[k].obj];
			const elf_symbol *y = &o->sym[ctx->output.got_locals.v[k].sym];
			bool abs = y->st_shndx == SHN_ABS;
			uint64_t v = abs ? y->st_value : ld_input_section_address(o->isec0 + y->st_shndx, y->st_value);
			uint64_t at = 8 * ((uint64_t)ctx->output.got_symbols.n + k);

			ld_u64_store(dst + at, v);
			if (ctx->relocations.pic && !abs) {
				dyn[nd].r_offset = ld_input_section_address(si, at);
				dyn[nd].r_info = R_AARCH64_RELATIVE;
				dyn[nd].r_addend = (int64_t)v;
				nd++;
			}
		}
		break;
	}
	case LD_SECTION_KIND_BUILD_ID:
		ld_u32_store(dst, 4);
		ld_u32_store(dst + 4, (uint32_t)is->size - 16);
		ld_u32_store(dst + 8, 3);
		memcpy(dst + 12, "GNU", 4);
		break;
	default:
		break;
	}
}
