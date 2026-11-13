/* Copying each input section into the output image and applying its relocations, in parallel. */

static VVEC(uint32_t) wjobs;		/* ISecs to write */

static void write_isec(void *arg, size_t j)
{
	(void)arg;
	uint32_t si = wjobs.v[j];
	ISec *is = &isecs.v[si];
	uint32_t oi = isec_osec(si);
	OSec *os = &osecs.v[oi];
	if (os->type == SHT_NOBITS || is->type == SHT_NOBITS)
		return;
	uint8_t *dst = out + os->offset + is->off;
	switch (is->kind) {
	case K_NORMAL:
		memcpy(dst, is->data, is->size);
		if (is->ehcie)	/* an FDE's CIE pointer: from the field to its CIE */
			wr32(dst + 4, (uint32_t)(isec_va(si, 4) - isec_va(is->ehcie - 1, 0)));
		if (is->nrela && (is->flags & SHF_ALLOC || !opt.relocatable)) {
			if (x86_ld)
				x64_relocate_isec(si, dst);
			else
				relocate_isec(si, dst);
		}
		break;
	case K_MERGE: {
		Merge *mg = &merges.v[is->aux];
		for (int s = 0; s < 32; s++)
			for (uint32_t k = 0; k < mg->uniq[s].n; k++) {
				uint64_t u = mg->uniq[s].v[k];
				MergeIn *m = &mergeins.v[u >> 32];
				uint32_t p = (uint32_t)u;
				memcpy(dst + m->outoff[p], isecs.v[m->isec].data + m->inoff[p], piece_len(m, p));
			}
		break;
	}
	case K_THUNK: {
		ThunkSec *ts = &thunksecs.v[is->aux];
		for (uint32_t k = 0; k < ts->list.n; k++) {
			uint32_t t = ts->list.v[k];
			uint8_t *b = dst + thunks.v[t].off;
			uint64_t s = thunk_dest(t), p = thunk_va(t);
			if (!thunks.v[t].lng) {
				wr32(b, 0x14000000 | (uint32_t)(((s - p) & 0x0ffffffc) >> 2));
				continue;
			}
			int64_t pg = (int64_t)((s & ~0xfffULL) - (p & ~0xfffULL));
			uint64_t imm = (uint64_t)pg >> 12;
			uint32_t adrp = 0x90000010 | (uint32_t)((imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5);
			wr32(b, adrp);
			wr32(b + 4, 0x91000210 | (uint32_t)(s & 0xfff) << 10);
			wr32(b + 8, 0xd61f0200);
		}
		break;
	}
	case K_GOT: {
		Rela *dyn = reladyn_buf ? reladyn_buf + is->dyn0 : NULL;
		uint32_t nd = 0;
		for (uint32_t k = 0; k < gotsyms.n; k++) {
			uint32_t g = gotsyms.v[k];
			bool undef = gsyms[g].state == GS_UNDEF || gsyms[g].demoted;
			uint64_t v = undef ? 0 : gsym_va(g);
			wr64(dst + 8 * k, v);
			if (pic && !undef && !gsym_isabs(g)) {
				dyn[nd].r_offset = isec_va(si, 8 * k);
				dyn[nd].r_info = R_AARCH64_RELATIVE;
				dyn[nd].r_addend = (int64_t)v;
				nd++;
			}
		}
		for (uint32_t k = 0; k < gotlocs.n; k++) {
			const Obj *o = objs.v[gotlocs.v[k].obj];
			const Sym *y = &o->sym[gotlocs.v[k].sym];
			bool abs = y->st_shndx == SHN_ABS;
			uint64_t v = abs ? y->st_value : isec_va(o->isec0 + y->st_shndx, y->st_value);
			uint64_t at = 8 * ((uint64_t)gotsyms.n + k);

			wr64(dst + at, v);
			if (pic && !abs) {
				dyn[nd].r_offset = isec_va(si, at);
				dyn[nd].r_info = R_AARCH64_RELATIVE;
				dyn[nd].r_addend = (int64_t)v;
				nd++;
			}
		}
		break;
	}
	case K_BUILDID:
		wr32(dst, 4);
		wr32(dst + 4, (uint32_t)is->size - 16);
		wr32(dst + 8, 3);
		memcpy(dst + 12, "GNU", 4);
		break;
	default:
		break;
	}
}
