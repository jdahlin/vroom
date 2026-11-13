/* Merging .eh_frame input into one output section as lld's EhFrameSection does: CIE/FDE splitting, deduplication and pieces. */

/* ---------------- .eh_frame (lld's EhFrameSection) ---------------- */

/* an input .eh_frame split into CIE and FDE records (lld EhInputSection::split) */
typedef struct { uint32_t off, size, r0, r1, cieoff; bool cie; } EhRec;

static bool eh_input(const ISec *is)
{
	return is->kind == K_NORMAL && is->obj != NONE && (is->flags & SHF_ALLOC) && !strcmp(is->name, ".eh_frame") &&
	       !opt.relocatable;
}

/* the records of input section si, relocations given to the record holding them; NULL on error */
static EhRec *eh_split(uint32_t si, uint32_t *nrec)
{
	const ISec *is = &isecs.v[si];
	const char *path = objs.v[is->obj]->path;
	EhRec *v = NULL;
	uint32_t n = 0, cap = 0, r = 0;

	for (uint64_t off = 0; off + 4 <= is->size;) {
		uint32_t len = rd32(is->data + off);

		if (len == 0xffffffffu) {
			error("%s:(.eh_frame+0x%llx): 64-bit DWARF records are not supported", path, (unsigned long long)off);
			free(v);
			return NULL;
		}
		if (len == 0)	/* a terminator */
			break;
		if (len < 4 || off + 4 + len > is->size) {
			error("%s:(.eh_frame+0x%llx): CIE/FDE too small", path, (unsigned long long)off);
			free(v);
			return NULL;
		}
		uint32_t id = rd32(is->data + off + 4);
		EhRec e = {.off = (uint32_t)off, .size = len + 4, .cie = id == 0};

		if (!e.cie) {
			if (id > off + 4) {
				error("%s:(.eh_frame+0x%llx): invalid CIE reference", path, (unsigned long long)off);
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
			error("%s:(.eh_frame): relocations are not sorted", path);
			free(v);
			return NULL;
		}
	*nrec = n;
	return v;
}

/* the input section relocation symbol y of object o is in, or NONE */
static uint32_t rel_isec(const Obj *o, uint32_t y)
{
	if (y >= o->firstg) {
		const G *g = &gsyms[o->gs[y - o->firstg]];

		return g->state == GS_OBJ ? g->sec : NONE;
	}
	return o->sym[y].st_shndx && o->sym[y].st_shndx < 0xff00 ? o->isec0 + o->sym[y].st_shndx : NONE;
}

static bool eh_live(uint32_t s)
{
	return s != NONE && isecs.v[s].kind != K_NONE && !isecs.v[s].dropped && isecs.v[s].osec != OS_DISCARD;
}

/* a CIE's identity: its bytes and its relocations (type, place, addend, target) */
static bool cie_same(uint32_t sa, const EhRec *a, uint32_t sb, const EhRec *b)
{
	const ISec *x = &isecs.v[sa], *y = &isecs.v[sb];
	const Obj *ox = objs.v[x->obj], *oy = objs.v[y->obj];

	if (a->size != b->size || a->r1 - a->r0 != b->r1 - b->r0 || memcmp(x->data + a->off, y->data + b->off, a->size))
		return false;
	for (uint32_t k = 0; k < a->r1 - a->r0; k++) {
		const Rela *p = &x->rela[a->r0 + k], *q = &y->rela[b->r0 + k];
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
static uint32_t eh_piece(uint32_t si, const EhRec *e)
{
	ISec src = isecs.v[si];
	uint32_t p = new_isec(src.name, K_NORMAL, src.type, src.flags, 1);
	ISec *is = &isecs.v[p];
	Rela *rl = e->r1 > e->r0 ? alloc((e->r1 - e->r0) * sizeof(Rela)) : NULL;

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
static void eh_frames(void)
{
	for (uint32_t d = 0; d < isds.n; d++) {
		ISD *id = &isds.v[d];
		uint32_t first = NONE;

		for (uint32_t k = 0; k < id->secs.n && first == NONE; k++)
			if (eh_input(&isecs.v[id->secs.v[k]]))
				first = k;
		if (first == NONE)
			continue;
		/* CIE records: (section, record, its FDE pieces) */
		struct Cie { uint32_t sec; EhRec rec; VVEC(uint32_t) fdes; } *cies = NULL;
		uint32_t ncie = 0, capc = 0;
		VVEC(uint32_t) rest = {0};

		for (uint32_t k = 0; k < id->secs.n; k++) {
			uint32_t si = id->secs.v[k];

			if (!eh_input(&isecs.v[si])) {
				VVEC_PUSH(rest, si, alloc);
				continue;
			}
			uint32_t nrec;
			EhRec *rec = eh_split(si, &nrec);

			if (!rec) {
				isecs.v[si].kind = K_NONE;
				continue;
			}
			const Obj *o = objs.v[isecs.v[si].obj];

			for (uint32_t r = 0; r < nrec; r++) {
				if (rec[r].cie)
					continue;
				const ISec *is = &isecs.v[si];

				if (rec[r].r0 == rec[r].r1 || !eh_live(rel_isec(o, is->rela[rec[r].r0].r_info >> 32)))
					continue;	/* lld: an FDE without a live target */
				const EhRec *ce = NULL;

				for (uint32_t q = 0; q < nrec; q++)
					if (rec[q].cie && rec[q].off == rec[r].cieoff)
						ce = &rec[q];
				if (!ce) {
					error("%s:(.eh_frame+0x%x): invalid CIE reference", o->path, rec[r].off);
					continue;
				}
				uint32_t c = 0;

				while (c < ncie && !cie_same(cies[c].sec, &cies[c].rec, si, ce))
					c++;
				if (c == ncie) {
					if (ncie == capc) {
						capc = capc ? 2 * capc : 8;
						cies = realloc(cies, capc * sizeof *cies);
					}
					cies[ncie++] = (struct Cie){ .sec = si, .rec = *ce };
				}
				uint32_t p = eh_piece(si, &rec[r]);

				VVEC_PUSH(cies[c].fdes, p, alloc);
			}
			free(rec);
			isecs.v[si].kind = K_NONE;	/* replaced by its records */
		}
		/* the records in place of the first .eh_frame input */
		VVEC(uint32_t) out = {0};

		for (uint32_t k = 0; k < first; k++)
			VVEC_PUSH(out, rest.v[k], alloc);
		uint32_t out0 = out.n;

		for (uint32_t c = 0; c < ncie; c++) {
			uint32_t cp = eh_piece(cies[c].sec, &cies[c].rec);

			VVEC_PUSH(out, cp, alloc);
			for (uint32_t f = 0; f < cies[c].fdes.n; f++) {
				isecs.v[cies[c].fdes.v[f]].ehcie = cp + 1;
				VVEC_PUSH(out, cies[c].fdes.v[f], alloc);
				VVEC_PUSH(ctx->layout.eh_frame_fdes, cies[c].fdes.v[f], alloc);
			}
		}
		{
			/* lld: a zero-length CIE as the terminator glibc expects; the section word aligned */
			static const uint8_t zero4[4];
			EhRec t = {.size = 4};
			uint32_t tp = eh_piece(id->secs.v[first], &t);

			isecs.v[tp].data = zero4;
			isecs.v[tp].nrela = 0;
			VVEC_PUSH(out, tp, alloc);
			isecs.v[out.v[out0]].align = 8;
		}
		for (uint32_t k = first; k < rest.n; k++)
			VVEC_PUSH(out, rest.v[k], alloc);
		id = &isds.v[d];
		id->secs.v = out.v;
		id->secs.n = out.n;
		id->secs.cap = out.cap;
		free(cies);
	}
}
