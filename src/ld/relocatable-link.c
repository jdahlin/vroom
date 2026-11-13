/* ld -r: relocatable output. */

/* ================= ld -r ================= */

/*
 * Relocatable output (vmlinux.o for modpost).  Input sections are grouped
 * by name into output sections in order of first appearance, contents are
 * copied unrelocated and every relocation is re-emitted: section-symbol
 * references move to the output section's symbol with the input section's
 * offset folded into the addend.  SHF_MERGE sections are concatenated, not
 * deduplicated, and .llvm_addrsig is dropped (its symbol indices would be
 * stale).  Byte identity with lld is not a goal here; modpost's view is.
 */
typedef struct {
	const char *name;
	uint32_t h;
	uint32_t type, align, entsize;
	uint64_t flags, size, off;
	VVEC(uint32_t) secs;
	uint64_t nrela, relaoff;
	uint32_t shidx, relaidx, nameoff, relanameoff;
	uint32_t secsym;
	uint32_t grp;			/* a .group: its input SHT_GROUP ISec + 1 */
	bool member;			/* a group member: its own output section, SHF_GROUP */
} ROut;
static VVEC(ROut) routs;
static uint32_t *rmap;			/* name hash table -> ROut */
static uint32_t rmask;
static uint32_t **lmap;			/* per object: local symbol -> output index */
static uint8_t *rbuf;			/* output */
static uint64_t r_symoff, r_stroff, r_shstroff, r_shstrsize;
static uint64_t r_nsyms, r_nlocal;
static uint32_t r_symidx, r_stridx, r_shstridx, r_nsh;
static uint64_t *r_locstr;		/* per object string offset of its locals */
static uint64_t *r_loc0;

static uint32_t rout_get(const char *name)
{
	uint32_t h = strhash(name, strlen(name));
	uint32_t j = h & rmask;
	while (rmap[j] != NONE) {
		ROut *r = &routs.v[rmap[j]];
		if (r->h == h && !strcmp(r->name, name))
			return rmap[j];
		j = (j + 1) & rmask;
	}
	ROut r = {.name = name, .h = h, .align = 1};
	VVEC_PUSH(routs, r, alloc);
	rmap[j] = routs.n - 1;
	return routs.n - 1;
}

/* an output section of its own (groups and their members, as lld -r) */
static uint32_t rout_new(const char *name)
{
	ROut r = {.name = name, .h = strhash(name, strlen(name)), .align = 1};
	VVEC_PUSH(routs, r, alloc);
	return routs.n - 1;
}

static void r_count_locals(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	uint32_t n = 0;
	uint64_t str = 0;
	if (o->buf)
		for (uint32_t s = 1; s < o->firstg && s < o->nsym; s++) {
			const Sym *y = &o->sym[s];
			if ((y->st_info & 15) == STT_SECTION)
				continue;
			n++;
			str += strlen(o->str + y->st_name) + 1;
		}
	o->nloc = n;
	o->locstr = str;
	lmap[i] = o->firstg ? alloc(o->firstg * sizeof(uint32_t)) : NULL;
}

static void r_write_locals(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	if (!o->buf)
		return;
	Sym *d = (Sym *)(rbuf + r_symoff) + r_loc0[i];
	uint32_t idx = r_loc0[i];
	uint64_t so = r_locstr[i];
	uint8_t *strbuf = rbuf + r_stroff;
	for (uint32_t s = 1; s < o->firstg && s < o->nsym; s++) {
		const Sym *y = &o->sym[s];
		if ((y->st_info & 15) == STT_SECTION) {
			uint32_t sec = o->isec0 + y->st_shndx;
			lmap[i][s] = y->st_shndx < o->nsh && isecs.v[sec].kind != K_NONE ?
				     routs.v[isecs.v[sec].osec].secsym : 0;
			continue;
		}
		uint16_t shndx = y->st_shndx;
		uint64_t val = y->st_value;
		if (shndx != SHN_UNDEF && shndx < 0xff00) {
			ISec *is = &isecs.v[o->isec0 + shndx];
			if (is->kind == K_NONE) {
				shndx = SHN_UNDEF;
				val = 0;
			} else {
				shndx = routs.v[is->osec].shidx;
				val += is->off;
			}
		}
		lmap[i][s] = idx++;
		put_sym(d++, strbuf, &so, o->str + y->st_name, y->st_info, y->st_other, shndx, val, y->st_size);
	}
}

static void r_write_globals(void *arg, size_t c)
{
	(void)arg;
	uint32_t b = c * 4096, e = b + 4096 < gorder.n ? b + 4096 : gorder.n;
	uint8_t *strbuf = rbuf + r_stroff;
	for (uint32_t i = b; i < e; i++) {
		uint32_t g = gorder.v[i];
		G *s = &gsyms[g];
		Sym *d = (Sym *)(rbuf + r_symoff) + s->outidx;
		uint64_t so = s->val;		/* string offset parked here, see below */
		uint16_t shndx = SHN_UNDEF;
		uint64_t val = 0, size = 0;
		if (s->state == GS_OBJ) {
			size = s->size;
			if (s->sec == NONE) {
				shndx = SHN_ABS;
				val = objs.v[s->obj]->sym[s->symidx].st_value;
			} else if (isecs.v[s->sec].kind == K_NONE) {
				size = 0;	/* in a dropped group member: undefined (lld) */
			} else {
				ISec *is = &isecs.v[s->sec];
				shndx = routs.v[is->osec].shidx;
				val = is->off + objs.v[s->obj]->sym[s->symidx].st_value;
			}
		}
		put_sym(d, strbuf, &so, gname(g), (uint8_t)(s->bind << 4 | s->type), s->vis, shndx, val, size);
	}
}

static void r_write_isec(void *arg, size_t j)
{
	(void)arg;
	uint32_t si = wjobs.v[j];
	ISec *is = &isecs.v[si];
	ROut *r = &routs.v[is->osec];
	if (is->type != SHT_NOBITS && r->type != SHT_NOBITS && is->size)
		memcpy(rbuf + r->off + is->off, is->data, is->size);
	if (!is->nrela)
		return;
	Obj *o = objs.v[is->obj];
	Rela *d = (Rela *)(rbuf + r->relaoff) + is->dyn0;
	for (uint32_t k = 0; k < is->nrela; k++) {
		const Rela *rl = &is->rela[k];
		uint32_t symi = rl->r_info >> 32, type = (uint32_t)rl->r_info;
		int64_t addend = rl->r_addend;
		uint32_t ns;
		if (symi == 0) {
			ns = 0;
		} else if (symi >= o->firstg) {
			ns = gsyms[o->gs[symi - o->firstg]].outidx;
		} else {
			const Sym *y = &o->sym[symi];
			ns = lmap[is->obj][symi];
			if ((y->st_info & 15) == STT_SECTION && y->st_shndx < o->nsh) {
				ISec *t = &isecs.v[o->isec0 + y->st_shndx];
				if (t->kind == K_NONE)
					error("%s:(%s): relocation against discarded section %s", o->path, is->name, t->name);
				else
					addend += t->off;
			}
		}
		d[k].r_offset = rl->r_offset + is->off;
		d[k].r_info = (uint64_t)ns << 32 | type;
		d[k].r_addend = addend;
	}
}

static int ld_relocatable(void)
{
	/* global order and final binding */
	build_gorder();
	pfor(gorder.n, finish_syms, NULL);

	/* output sections by name, first appearance */
	uint32_t cap = 1024;
	while (cap < isecs.n)
		cap *= 2;
	rmap = alloc(cap * sizeof(uint32_t));
	memset(rmap, 0xff, cap * sizeof(uint32_t));
	rmask = cap - 1;
	for (uint32_t i = 0; i < isecs.n; i++) {
		ISec *is = &isecs.v[i];
		const Obj *io = is->obj != NONE ? objs.v[is->obj] : NULL;
		const Shdr *ih = io && is->shndx < io->nsh ? &io->sh[is->shndx] : NULL;

		if (ih && ih->sh_type == SHT_GROUP) {
			/* a group whose members are kept (a later COMDAT copy is dropped) */
			const uint32_t *w = (const uint32_t *)(io->buf + ih->sh_offset);
			uint32_t nw = (uint32_t)(ih->sh_size / 4);

			if (nw < 2 || isecs.v[io->isec0 + w[1]].dropped)
				continue;
			uint32_t gi = rout_new(".group");
			ROut *g = &routs.v[gi];

			g->type = SHT_GROUP;
			g->align = 4;
			g->entsize = 4;
			g->grp = i + 1;
			g->size = 4 * (uint64_t)nw;
			is->osec = gi;
			continue;
		}
		if (is->kind != K_NORMAL)
			continue;
		bool member = ih && (ih->sh_flags & SHF_GROUP);
		uint32_t ri = member ? rout_new(is->name) : rout_get(is->name);
		ROut *r = &routs.v[ri];

		if (member) {
			r->member = true;
			is->flags |= SHF_GROUP;
		}
		if (!r->secs.n) {
			r->type = is->type;
			r->flags = is->flags;
			r->entsize = is->entsize;
		} else {
			if (r->type != is->type && !(r->type == SHT_NOBITS && is->type == SHT_NOBITS))
				r->type = SHT_PROGBITS;
			if (r->entsize != is->entsize)
				r->entsize = 0;
			r->flags |= is->flags;
		}
		if (is->align > r->align)
			r->align = is->align;
		is->osec = ri;
		r->size = alignup(r->size, is->align);
		is->off = r->size;
		r->size += is->size;
		is->dyn0 = r->nrela;
		r->nrela += is->nrela;
		VVEC_PUSH(r->secs, i, alloc);
	}
	phase("-r: output sections");

	/* section header indices and names */
	uint32_t sh = 1;
	uint64_t shs = 1;
	for (uint32_t i = 0; i < routs.n; i++) {
		ROut *r = &routs.v[i];
		r->shidx = sh++;
		r->nameoff = shs;
		shs += strlen(r->name) + 1;
		if (r->nrela) {
			r->relaidx = sh++;
			r->relanameoff = shs;
			shs += strlen(r->name) + 6;
		}
	}
	if (sh > 0xff00) {
		error("-r: too many output sections (%u)", sh);
		return 1;
	}
	r_symidx = sh++;
	r_stridx = sh++;
	r_shstridx = sh++;
	r_nsh = sh;
	uint64_t shs_symtab = shs; shs += 8;
	uint64_t shs_strtab = shs; shs += 8;
	uint64_t shs_shstrtab = shs; shs += 10;
	r_shstrsize = shs;

	/* symbols: null, section symbols, locals per file, globals */
	lmap = alloc(objs.n * sizeof(uint32_t *));
	pfor(objs.n, r_count_locals, NULL);
	r_loc0 = alloc(objs.n * sizeof(uint64_t));
	r_locstr = alloc(objs.n * sizeof(uint64_t));
	uint64_t idx = 1 + routs.n, str = 1;
	for (uint32_t i = 0; i < routs.n; i++)
		routs.v[i].secsym = 1 + i;
	for (uint32_t i = 0; i < objs.n; i++) {
		r_loc0[i] = idx;
		r_locstr[i] = str;
		idx += objs.v[i]->nloc;
		str += objs.v[i]->locstr;
	}
	r_nlocal = idx;
	for (uint32_t i = 0; i < gorder.n; i++) {
		G *s = &gsyms[gorder.v[i]];
		s->outidx = idx++;
		/* s->val is unused for -r output: park the string offset there */
		s->val = str;
		str += strlen(gname(gorder.v[i])) + 1;
	}
	r_nsyms = idx;
	uint64_t strsz = str;

	/* file layout */
	uint64_t off = sizeof(Ehdr);
	for (uint32_t i = 0; i < routs.n; i++) {
		ROut *r = &routs.v[i];
		off = alignup(off, r->align);
		r->off = off;
		if (r->type != SHT_NOBITS)
			off += r->size;
	}
	off = alignup(off, 8);
	for (uint32_t i = 0; i < routs.n; i++) {
		ROut *r = &routs.v[i];
		if (!r->nrela)
			continue;
		r->relaoff = off;
		off += r->nrela * sizeof(Rela);
	}
	r_symoff = off;
	off += r_nsyms * sizeof(Sym);
	r_stroff = off;
	off += strsz;
	r_shstroff = off;
	off += r_shstrsize;
	uint64_t shoff_r = alignup(off, 8);
	uint64_t fsize = shoff_r + (uint64_t)r_nsh * sizeof(Shdr);
	rbuf = bigalloc(fsize);
	phase("-r: layout");

	/* contents and relocations */
	wjobs.n = 0;
	for (uint32_t i = 0; i < routs.n; i++)
		for (uint32_t k = 0; k < routs.v[i].secs.n; k++)
			VVEC_PUSH(wjobs, routs.v[i].secs.v[k], alloc);
	/* local maps must exist before relocations are rewritten */
	pfor(objs.n, r_write_locals, NULL);
	pfor(wjobs.n, r_write_isec, NULL);
	pfor((gorder.n + 4095) / 4096, r_write_globals, NULL);
	/* groups: the flag word, then the members' output indices (a .rela member: its section's) */
	for (uint32_t i = 0; i < routs.n; i++) {
		ROut *g = &routs.v[i];

		if (!g->grp)
			continue;
		const ISec *gs = &isecs.v[g->grp - 1];
		const Obj *o = objs.v[gs->obj];
		const Shdr *h = &o->sh[gs->shndx];
		const uint32_t *w = (const uint32_t *)(o->buf + h->sh_offset);
		uint32_t *d = (uint32_t *)(void *)(rbuf + g->off), n = 0;

		d[n++] = w[0];
		for (uint64_t k = 1; k < h->sh_size / 4; k++) {
			const Shdr *m = &o->sh[w[k]];
			const ISec *mi = &isecs.v[o->isec0 + (m->sh_type == SHT_RELA ? m->sh_info : w[k])];

			if (mi->kind == K_NONE || mi->osec == NONE)
				continue;
			uint32_t x = m->sh_type == SHT_RELA ? routs.v[mi->osec].relaidx : routs.v[mi->osec].shidx;

			if (x)
				d[n++] = x;
		}
		g->size = 4 * (uint64_t)n;
	}
	{
		Sym *sy = (Sym *)(rbuf + r_symoff);
		for (uint32_t i = 0; i < routs.n; i++)
			sy[1 + i] = (Sym){.st_info = STB_LOCAL << 4 | STT_SECTION, .st_shndx = routs.v[i].shidx};
	}
	phase("-r: copy, relocations, symbols");

	/* headers */
	char *shstr = (char *)rbuf + r_shstroff;
	Shdr *shd = (Shdr *)(rbuf + shoff_r);
	for (uint32_t i = 0; i < routs.n; i++) {
		ROut *r = &routs.v[i];
		strcpy(shstr + r->nameoff, r->name);
		shd[r->shidx] = (Shdr){.sh_name = r->nameoff, .sh_type = r->type, .sh_flags = r->flags,
				       .sh_offset = r->off, .sh_size = r->size, .sh_addralign = r->align,
				       .sh_entsize = r->entsize};
		if (r->grp) {	/* sh_link the symbol table, sh_info the signature */
			const ISec *gs = &isecs.v[r->grp - 1];
			const Obj *o = objs.v[gs->obj];
			uint32_t y = o->sh[gs->shndx].sh_info;

			shd[r->shidx].sh_size = r->size;
			shd[r->shidx].sh_link = r_symidx;
			shd[r->shidx].sh_info = y >= o->firstg ? gsyms[o->gs[y - o->firstg]].outidx : lmap[gs->obj][y];
		}
		if (r->nrela) {
			sprintf(shstr + r->relanameoff, ".rela%s", r->name);
			shd[r->relaidx] = (Shdr){.sh_name = r->relanameoff, .sh_type = SHT_RELA,
						 .sh_flags = SHF_INFO_LINK | (r->member ? SHF_GROUP : 0),
						 .sh_offset = r->relaoff, .sh_size = r->nrela * sizeof(Rela),
						 .sh_link = r_symidx, .sh_info = r->shidx, .sh_addralign = 8,
						 .sh_entsize = sizeof(Rela)};
		}
	}
	strcpy(shstr + shs_symtab, ".symtab");
	strcpy(shstr + shs_strtab, ".strtab");
	strcpy(shstr + shs_shstrtab, ".shstrtab");
	shd[r_symidx] = (Shdr){.sh_name = shs_symtab, .sh_type = SHT_SYMTAB, .sh_offset = r_symoff,
			       .sh_size = r_nsyms * sizeof(Sym), .sh_link = r_stridx, .sh_info = r_nlocal,
			       .sh_addralign = 8, .sh_entsize = sizeof(Sym)};
	shd[r_stridx] = (Shdr){.sh_name = shs_strtab, .sh_type = SHT_STRTAB, .sh_offset = r_stroff,
			       .sh_size = strsz, .sh_addralign = 1};
	shd[r_shstridx] = (Shdr){.sh_name = shs_shstrtab, .sh_type = SHT_STRTAB, .sh_offset = r_shstroff,
				 .sh_size = r_shstrsize, .sh_addralign = 1};
	Ehdr *eh = (Ehdr *)rbuf;
	memcpy(eh->e_ident, "\177ELF\2\1\1", 7);
	eh->e_type = 1;
	eh->e_machine = x86_ld ? 62 : 183;
	eh->e_version = 1;
	eh->e_shoff = shoff_r;
	eh->e_ehsize = sizeof(Ehdr);
	eh->e_shentsize = sizeof(Shdr);
	eh->e_shnum = r_nsh;
	eh->e_shstrndx = r_shstridx;
	if (atomic_load(&ctx->diagnostics.error_count)) {
		munmap(rbuf, fsize);
		return 1;
	}
	out = rbuf;
	ctx->layout.file_size = fsize;
	int rc = write_output();
	phase("-r: write");
	unmap(rbuf, fsize);
	out = NULL;
	for (uint32_t i = 0; i < objs.n; i++)
		if (objs.v[i]->mapped && objs.v[i]->buf)
			unmap(objs.v[i]->buf, objs.v[i]->size);
	gtable_free();
	memset(&routs, 0, sizeof routs);
	return rc || atomic_load(&ctx->diagnostics.error_count) ? 1 : 0;
}
