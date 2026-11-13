/* Merging .eh_frame input into one output section as lld's EhFrameSection does: CIE/FDE splitting, deduplication and pieces. */

/* ---------------- .eh_frame (lld's EhFrameSection) ---------------- */

/* an input .eh_frame split into CIE and FDE records (lld EhInputSection::split) */
typedef struct { uint32_t off, size, r0, r1, cieoff; bool cie; } ld_eh_frame_record;

static bool ld_eh_frame_is_input(struct ld_context *ctx, const ld_input_section *is)
{
	return is->kind == LD_SECTION_KIND_NORMAL && is->obj != LD_NONE && (is->flags & SHF_ALLOC) && !strcmp(is->name, ".eh_frame") &&
	       !ctx->options.relocatable;
}

/* the records of input section si, relocations given to the record holding them; NULL on error */
static ld_eh_frame_record *ld_eh_frame_records_split(struct ld_context *ctx, uint32_t si, uint32_t *nrec)
{
	const ld_input_section *is = &ctx->inputs.sections.v[si];
	const char *path = ctx->inputs.objects.v[is->obj]->path;
	ld_eh_frame_record *v = NULL;
	uint32_t n = 0, cap = 0, r = 0;

	for (uint64_t off = 0; off + 4 <= is->size;) {
		uint32_t len = ld_u32_load(is->data + off);

		if (len == 0xffffffffu) {
			ld_error_report(ctx, "%s:(.eh_frame+0x%llx): 64-bit DWARF records are not supported", path, (unsigned long long)off);
			free(v);
			return NULL;
		}
		if (len == 0)	/* a terminator */
			break;
		if (len < 4 || off + 4 + len > is->size) {
			ld_error_report(ctx, "%s:(.eh_frame+0x%llx): CIE/FDE too small", path, (unsigned long long)off);
			free(v);
			return NULL;
		}
		uint32_t id = ld_u32_load(is->data + off + 4);
		ld_eh_frame_record e = {.off = (uint32_t)off, .size = len + 4, .cie = id == 0};

		if (!e.cie) {
			if (id > off + 4) {
				ld_error_report(ctx, "%s:(.eh_frame+0x%llx): invalid CIE reference", path, (unsigned long long)off);
				free(v);
				return NULL;
			}
			e.cieoff = (uint32_t)(off + 4 - id);
		}
		while (r < is->nrela && is->rela[r].r_offset < off)
			r++;
		e.r0 = r;
		while (r < is->nrela && is->rela[r].r_offset < off + e.size)
			r++;
		e.r1 = r;
		if (n == cap) {
			cap = cap ? 2 * cap : 16;
			v = realloc(v, cap * sizeof *v);
		}
		v[n++] = e;
		off += e.size;
	}
	for (uint32_t k = 1; k < is->nrela; k++)
		if (is->rela[k].r_offset < is->rela[k - 1].r_offset) {
			ld_error_report(ctx, "%s:(.eh_frame): relocations are not sorted", path);
			free(v);
			return NULL;
		}
	*nrec = n;
	return v;
}

/* the input section relocation symbol y of object o is in, or LD_NONE */
static uint32_t ld_relocation_symbol_input_section(struct ld_context *ctx, const ld_object *o, uint32_t y)
{
	if (y >= o->firstg) {
		const ld_global_symbol *g = &ctx->symbols.globals[o->gs[y - o->firstg]];

		return g->state == LD_SYMBOL_STATE_OBJECT ? g->sec : LD_NONE;
	}
	return o->sym[y].st_shndx && o->sym[y].st_shndx < 0xff00 ? o->isec0 + o->sym[y].st_shndx : LD_NONE;
}

static bool ld_eh_frame_piece_is_live(struct ld_context *ctx, uint32_t s)
{
	return s != LD_NONE && ctx->inputs.sections.v[s].kind != LD_SECTION_KIND_NONE && !ctx->inputs.sections.v[s].dropped && ctx->inputs.sections.v[s].osec != LD_OUTPUT_SECTION_DISCARD;
}

/* a CIE's identity: its bytes and its relocations (type, place, addend, target) */
static bool ld_eh_frame_cie_is_same(struct ld_context *ctx, uint32_t sa, const ld_eh_frame_record *a, uint32_t sb, const ld_eh_frame_record *b)
{
	const ld_input_section *x = &ctx->inputs.sections.v[sa], *y = &ctx->inputs.sections.v[sb];
	const ld_object *ox = ctx->inputs.objects.v[x->obj], *oy = ctx->inputs.objects.v[y->obj];

	if (a->size != b->size || a->r1 - a->r0 != b->r1 - b->r0 || memcmp(x->data + a->off, y->data + b->off, a->size))
		return false;
	for (uint32_t k = 0; k < a->r1 - a->r0; k++) {
		const elf_rela *p = &x->rela[a->r0 + k], *q = &y->rela[b->r0 + k];
		uint32_t py = p->r_info >> 32, qy = q->r_info >> 32;

		if ((uint32_t)p->r_info != (uint32_t)q->r_info || p->r_addend != q->r_addend ||
		    p->r_offset - a->off != q->r_offset - b->off)
			return false;
		bool pg = py >= ox->firstg, qg = qy >= oy->firstg;

		if (pg != qg)
			return false;
		if (pg ? ox->gs[py - ox->firstg] != oy->gs[qy - oy->firstg] : (x->obj != y->obj || py != qy))
			return false;
	}
	return true;
}

/* a record as an input section of its own */
static uint32_t ld_eh_frame_record_section_make(struct ld_context *ctx, uint32_t si, const ld_eh_frame_record *e)
{
	ld_input_section src = ctx->inputs.sections.v[si];
	uint32_t p = ld_input_section_create(ctx, src.name, LD_SECTION_KIND_NORMAL, src.type, src.flags, 1);
	ld_input_section *is = &ctx->inputs.sections.v[p];
	elf_rela *rl = e->r1 > e->r0 ? ld_region_alloc((e->r1 - e->r0) * sizeof(elf_rela)) : NULL;

	for (uint32_t k = e->r0; k < e->r1; k++) {
		rl[k - e->r0] = src.rela[k];
		rl[k - e->r0].r_offset -= e->off;
	}
	is->obj = src.obj;
	is->shndx = src.shndx;
	is->isd = src.isd;
	is->data = src.data + e->off;
	is->size = e->size;
	is->rela = rl;
	is->nrela = e->r1 - e->r0;
	return p;
}

/*
 * The .eh_frame inputs of each input section description become records,
 * placed where the first of them was: a CIE once per identity, in the order
 * of first use by a live FDE, each followed by its FDEs; an FDE is kept when
 * the section its first relocation names is live (lld's isFdeLive), and a
 * CIE without FDEs is dropped (lld's EhFrameSection::addRecords/finalize).
 */
static void ld_eh_frame_merge(struct ld_context *ctx)
{
	for (uint32_t d = 0; d < ctx->script.input_descriptions.n; d++) {
		ld_input_section_description *id = &ctx->script.input_descriptions.v[d];
		uint32_t first = LD_NONE;

		for (uint32_t k = 0; k < id->secs.n && first == LD_NONE; k++)
			if (ld_eh_frame_is_input(ctx, &ctx->inputs.sections.v[id->secs.v[k]]))
				first = k;
		if (first == LD_NONE)
			continue;
		/* CIE records: (section, record, its FDE pieces) */
		struct Cie { uint32_t sec; ld_eh_frame_record rec; VVEC(uint32_t) fdes; } *cies = NULL;
		uint32_t ncie = 0, capc = 0;
		VVEC(uint32_t) rest = {0};

		for (uint32_t k = 0; k < id->secs.n; k++) {
			uint32_t si = id->secs.v[k];

			if (!ld_eh_frame_is_input(ctx, &ctx->inputs.sections.v[si])) {
				VVEC_PUSH(rest, si, ld_region_alloc);
				continue;
			}
			uint32_t nrec;
			ld_eh_frame_record *rec = ld_eh_frame_records_split(ctx, si, &nrec);

			if (!rec) {
				ctx->inputs.sections.v[si].kind = LD_SECTION_KIND_NONE;
				continue;
			}
			const ld_object *o = ctx->inputs.objects.v[ctx->inputs.sections.v[si].obj];

			for (uint32_t r = 0; r < nrec; r++) {
				if (rec[r].cie)
					continue;
				const ld_input_section *is = &ctx->inputs.sections.v[si];

				if (rec[r].r0 == rec[r].r1 || !ld_eh_frame_piece_is_live(ctx, ld_relocation_symbol_input_section(ctx, o, is->rela[rec[r].r0].r_info >> 32)))
					continue;	/* lld: an FDE without a live target */
				const ld_eh_frame_record *ce = NULL;

				for (uint32_t q = 0; q < nrec; q++)
					if (rec[q].cie && rec[q].off == rec[r].cieoff)
						ce = &rec[q];
				if (!ce) {
					ld_error_report(ctx, "%s:(.eh_frame+0x%x): invalid CIE reference", o->path, rec[r].off);
					continue;
				}
				uint32_t c = 0;

				while (c < ncie && !ld_eh_frame_cie_is_same(ctx, cies[c].sec, &cies[c].rec, si, ce))
					c++;
				if (c == ncie) {
					if (ncie == capc) {
						capc = capc ? 2 * capc : 8;
						cies = realloc(cies, capc * sizeof *cies);
					}
					cies[ncie++] = (struct Cie){ .sec = si, .rec = *ce };
				}
				uint32_t p = ld_eh_frame_record_section_make(ctx, si, &rec[r]);

				VVEC_PUSH(cies[c].fdes, p, ld_region_alloc);
			}
			free(rec);
			ctx->inputs.sections.v[si].kind = LD_SECTION_KIND_NONE;	/* replaced by its records */
		}
		/* the records in place of the first .eh_frame input */
		VVEC(uint32_t) out = {0};

		for (uint32_t k = 0; k < first; k++)
			VVEC_PUSH(out, rest.v[k], ld_region_alloc);
		uint32_t out0 = out.n;

		for (uint32_t c = 0; c < ncie; c++) {
			uint32_t cp = ld_eh_frame_record_section_make(ctx, cies[c].sec, &cies[c].rec);

			VVEC_PUSH(out, cp, ld_region_alloc);
			for (uint32_t f = 0; f < cies[c].fdes.n; f++) {
				ctx->inputs.sections.v[cies[c].fdes.v[f]].ehcie = cp + 1;
				VVEC_PUSH(out, cies[c].fdes.v[f], ld_region_alloc);
				VVEC_PUSH(ctx->layout.eh_frame_fdes, cies[c].fdes.v[f], ld_region_alloc);
			}
		}
		{
			/* lld: a zero-length CIE as the terminator glibc expects; the section word aligned */
			static const uint8_t zero4[4];
			ld_eh_frame_record t = {.size = 4};
			uint32_t tp = ld_eh_frame_record_section_make(ctx, id->secs.v[first], &t);

			ctx->inputs.sections.v[tp].data = zero4;
			ctx->inputs.sections.v[tp].nrela = 0;
			VVEC_PUSH(out, tp, ld_region_alloc);
			ctx->inputs.sections.v[out.v[out0]].align = 8;
		}
		for (uint32_t k = first; k < rest.n; k++)
			VVEC_PUSH(out, rest.v[k], ld_region_alloc);
		id = &ctx->script.input_descriptions.v[d];
		id->secs.v = out.v;
		id->secs.n = out.n;
		id->secs.cap = out.cap;
		free(cies);
	}
}
