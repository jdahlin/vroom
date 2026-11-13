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
} ld_relocatable_section;
static VVEC(ld_relocatable_section) routs;
static uint32_t *rmap;			/* name hash table -> ROut */
static uint32_t rmask;
static uint32_t **lmap;			/* per object: local symbol -> output index */
static uint8_t *rbuf;			/* output */
static uint64_t r_symoff, r_stroff, r_shstroff, r_shstrsize;
static uint64_t r_nsyms, r_nlocal;
static uint32_t r_symidx, r_stridx, r_shstridx, r_nsh;
static uint64_t *r_locstr;		/* per object string offset of its locals */
static uint64_t *r_loc0;

static uint32_t ld_relocatable_section_find(const char *name)
{
	uint32_t h = ld_name_hash(name, strlen(name));
	uint32_t j = h & rmask;
	while (rmap[j] != LD_NONE) {
		ld_relocatable_section *r = &routs.v[rmap[j]];
		if (r->h == h && !strcmp(r->name, name))
			return rmap[j];
		j = (j + 1) & rmask;
	}
	ld_relocatable_section r = {.name = name, .h = h, .align = 1};
	VVEC_PUSH(routs, r, ld_region_alloc);
	rmap[j] = routs.n - 1;
	return routs.n - 1;
}

/* an output section of its own (groups and their members, as lld -r) */
static uint32_t ld_relocatable_section_create(const char *name)
{
	ld_relocatable_section r = {.name = name, .h = ld_name_hash(name, strlen(name)), .align = 1};
	VVEC_PUSH(routs, r, ld_region_alloc);
	return routs.n - 1;
}

static void ld_relocatable_locals_count(void *arg, size_t i)
{
	(void)arg;
	ld_object *o = objs.v[i];
	uint32_t n = 0;
	uint64_t str = 0;
	if (o->buf)
		for (uint32_t s = 1; s < o->firstg && s < o->nsym; s++) {
			const elf_symbol *y = &o->sym[s];
			if ((y->st_info & 15) == STT_SECTION)
				continue;
			n++;
			str += strlen(o->str + y->st_name) + 1;
		}
	o->nloc = n;
	o->locstr = str;
	lmap[i] = o->firstg ? ld_region_alloc(o->firstg * sizeof(uint32_t)) : NULL;
}

static void ld_relocatable_locals_write(void *arg, size_t i)
{
	(void)arg;
	ld_object *o = objs.v[i];
	if (!o->buf)
		return;
	elf_symbol *d = (elf_symbol *)(rbuf + r_symoff) + r_loc0[i];
	uint32_t idx = r_loc0[i];
	uint64_t so = r_locstr[i];
	uint8_t *strbuf = rbuf + r_stroff;
	for (uint32_t s = 1; s < o->firstg && s < o->nsym; s++) {
		const elf_symbol *y = &o->sym[s];
		if ((y->st_info & 15) == STT_SECTION) {
			uint32_t sec = o->isec0 + y->st_shndx;
			lmap[i][s] = y->st_shndx < o->nsh && isecs.v[sec].kind != LD_SECTION_KIND_NONE ?
				     routs.v[isecs.v[sec].osec].secsym : 0;
			continue;
		}
		uint16_t shndx = y->st_shndx;
		uint64_t val = y->st_value;
		if (shndx != SHN_UNDEF && shndx < 0xff00) {
			ld_input_section *is = &isecs.v[o->isec0 + shndx];
			if (is->kind == LD_SECTION_KIND_NONE) {
				shndx = SHN_UNDEF;
				val = 0;
			} else {
				shndx = routs.v[is->osec].shidx;
				val += is->off;
			}
		}
		lmap[i][s] = idx++;
		ld_symbol_record_store(d++, strbuf, &so, o->str + y->st_name, y->st_info, y->st_other, shndx, val, y->st_size);
	}
}

static void ld_relocatable_globals_write(void *arg, size_t c)
{
	(void)arg;
	uint32_t b = c * 4096, e = b + 4096 < gorder.n ? b + 4096 : gorder.n;
	uint8_t *strbuf = rbuf + r_stroff;
	for (uint32_t i = b; i < e; i++) {
		uint32_t g = gorder.v[i];
		ld_global_symbol *s = &gsyms[g];
		elf_symbol *d = (elf_symbol *)(rbuf + r_symoff) + s->outidx;
		uint64_t so = s->val;		/* string offset parked here, see below */
		uint16_t shndx = SHN_UNDEF;
		uint64_t val = 0, size = 0;
		if (s->state == LD_SYMBOL_STATE_OBJECT) {
			size = s->size;
			if (s->sec == LD_NONE) {
				shndx = SHN_ABS;
				val = objs.v[s->obj]->sym[s->symidx].st_value;
			} else if (isecs.v[s->sec].kind == LD_SECTION_KIND_NONE) {
				size = 0;	/* in a dropped group member: undefined (lld) */
			} else {
				ld_input_section *is = &isecs.v[s->sec];
				shndx = routs.v[is->osec].shidx;
				val = is->off + objs.v[s->obj]->sym[s->symidx].st_value;
			}
		}
		ld_symbol_record_store(d, strbuf, &so, ld_global_name(g), (uint8_t)(s->bind << 4 | s->type), s->vis, shndx, val, size);
	}
}

static void ld_relocatable_input_section_write(void *arg, size_t j)
{
	(void)arg;
	uint32_t si = wjobs.v[j];
	ld_input_section *is = &isecs.v[si];
	ld_relocatable_section *r = &routs.v[is->osec];
	if (is->type != SHT_NOBITS && r->type != SHT_NOBITS && is->size)
		memcpy(rbuf + r->off + is->off, is->data, is->size);
	if (!is->nrela)
		return;
	ld_object *o = objs.v[is->obj];
	elf_rela *d = (elf_rela *)(rbuf + r->relaoff) + is->dyn0;
	for (uint32_t k = 0; k < is->nrela; k++) {
		const elf_rela *rl = &is->rela[k];
		uint32_t symi = rl->r_info >> 32, type = (uint32_t)rl->r_info;
		int64_t addend = rl->r_addend;
		uint32_t ns;
		if (symi == 0) {
			ns = 0;
		} else if (symi >= o->firstg) {
			ns = gsyms[o->gs[symi - o->firstg]].outidx;
		} else {
			const elf_symbol *y = &o->sym[symi];
			ns = lmap[is->obj][symi];
			if ((y->st_info & 15) == STT_SECTION && y->st_shndx < o->nsh) {
				ld_input_section *t = &isecs.v[o->isec0 + y->st_shndx];
				if (t->kind == LD_SECTION_KIND_NONE)
					ld_error_report("%s:(%s): relocation against discarded section %s", o->path, is->name, t->name);
				else
					addend += t->off;
			}
		}
		d[k].r_offset = rl->r_offset + is->off;
		d[k].r_info = (uint64_t)ns << 32 | type;
		d[k].r_addend = addend;
	}
}

static int ld_relocatable_link(void)
{
	/* global order and final binding */
	ld_global_order_build();
	ld_parallel_for(gorder.n, ld_global_symbol_finish_task, NULL);

	/* output sections by name, first appearance */
	uint32_t cap = 1024;
	while (cap < isecs.n)
		cap *= 2;
	rmap = ld_region_alloc(cap * sizeof(uint32_t));
	memset(rmap, 0xff, cap * sizeof(uint32_t));
	rmask = cap - 1;
	for (uint32_t i = 0; i < isecs.n; i++) {
		ld_input_section *is = &isecs.v[i];
		const ld_object *io = is->obj != LD_NONE ? objs.v[is->obj] : NULL;
		const elf_section_header *ih = io && is->shndx < io->nsh ? &io->sh[is->shndx] : NULL;

		if (ih && ih->sh_type == SHT_GROUP) {
			/* a group whose members are kept (a later COMDAT copy is dropped) */
			const uint32_t *w = (const uint32_t *)(io->buf + ih->sh_offset);
			uint32_t nw = (uint32_t)(ih->sh_size / 4);

			if (nw < 2 || isecs.v[io->isec0 + w[1]].dropped)
				continue;
			uint32_t gi = ld_relocatable_section_create(".group");
			ld_relocatable_section *g = &routs.v[gi];

			g->type = SHT_GROUP;
			g->align = 4;
			g->entsize = 4;
			g->grp = i + 1;
			g->size = 4 * (uint64_t)nw;
			is->osec = gi;
			continue;
		}
		if (is->kind != LD_SECTION_KIND_NORMAL)
			continue;
		bool member = ih && (ih->sh_flags & SHF_GROUP);
		uint32_t ri = member ? ld_relocatable_section_create(is->name) : ld_relocatable_section_find(is->name);
		ld_relocatable_section *r = &routs.v[ri];

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
		r->size = ld_value_align_up(r->size, is->align);
		is->off = r->size;
		r->size += is->size;
		is->dyn0 = r->nrela;
		r->nrela += is->nrela;
		VVEC_PUSH(r->secs, i, ld_region_alloc);
	}
	ld_phase_time_report("-r: output sections");

	/* section header indices and names */
	uint32_t sh = 1;
	uint64_t shs = 1;
	for (uint32_t i = 0; i < routs.n; i++) {
		ld_relocatable_section *r = &routs.v[i];
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
		ld_error_report("-r: too many output sections (%u)", sh);
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
	lmap = ld_region_alloc(objs.n * sizeof(uint32_t *));
	ld_parallel_for(objs.n, ld_relocatable_locals_count, NULL);
	r_loc0 = ld_region_alloc(objs.n * sizeof(uint64_t));
	r_locstr = ld_region_alloc(objs.n * sizeof(uint64_t));
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
		ld_global_symbol *s = &gsyms[gorder.v[i]];
		s->outidx = idx++;
		/* s->val is unused for -r output: park the string offset there */
		s->val = str;
		str += strlen(ld_global_name(gorder.v[i])) + 1;
	}
	r_nsyms = idx;
	uint64_t strsz = str;

	/* file layout */
	uint64_t off = sizeof(elf_file_header);
	for (uint32_t i = 0; i < routs.n; i++) {
		ld_relocatable_section *r = &routs.v[i];
		off = ld_value_align_up(off, r->align);
		r->off = off;
		if (r->type != SHT_NOBITS)
			off += r->size;
	}
	off = ld_value_align_up(off, 8);
	for (uint32_t i = 0; i < routs.n; i++) {
		ld_relocatable_section *r = &routs.v[i];
		if (!r->nrela)
			continue;
		r->relaoff = off;
		off += r->nrela * sizeof(elf_rela);
	}
	r_symoff = off;
	off += r_nsyms * sizeof(elf_symbol);
	r_stroff = off;
	off += strsz;
	r_shstroff = off;
	off += r_shstrsize;
	uint64_t shoff_r = ld_value_align_up(off, 8);
	uint64_t fsize = shoff_r + (uint64_t)r_nsh * sizeof(elf_section_header);
	rbuf = ld_region_large_alloc(fsize);
	ld_phase_time_report("-r: layout");

	/* contents and relocations */
	wjobs.n = 0;
	for (uint32_t i = 0; i < routs.n; i++)
		for (uint32_t k = 0; k < routs.v[i].secs.n; k++)
			VVEC_PUSH(wjobs, routs.v[i].secs.v[k], ld_region_alloc);
	/* local maps must exist before relocations are rewritten */
	ld_parallel_for(objs.n, ld_relocatable_locals_write, NULL);
	ld_parallel_for(wjobs.n, ld_relocatable_input_section_write, NULL);
	ld_parallel_for((gorder.n + 4095) / 4096, ld_relocatable_globals_write, NULL);
	/* groups: the flag word, then the members' output indices (a .rela member: its section's) */
	for (uint32_t i = 0; i < routs.n; i++) {
		ld_relocatable_section *g = &routs.v[i];

		if (!g->grp)
			continue;
		const ld_input_section *gs = &isecs.v[g->grp - 1];
		const ld_object *o = objs.v[gs->obj];
		const elf_section_header *h = &o->sh[gs->shndx];
		const uint32_t *w = (const uint32_t *)(o->buf + h->sh_offset);
		uint32_t *d = (uint32_t *)(void *)(rbuf + g->off), n = 0;

		d[n++] = w[0];
		for (uint64_t k = 1; k < h->sh_size / 4; k++) {
			const elf_section_header *m = &o->sh[w[k]];
			const ld_input_section *mi = &isecs.v[o->isec0 + (m->sh_type == SHT_RELA ? m->sh_info : w[k])];

			if (mi->kind == LD_SECTION_KIND_NONE || mi->osec == LD_NONE)
				continue;
			uint32_t x = m->sh_type == SHT_RELA ? routs.v[mi->osec].relaidx : routs.v[mi->osec].shidx;

			if (x)
				d[n++] = x;
		}
		g->size = 4 * (uint64_t)n;
	}
	{
		elf_symbol *sy = (elf_symbol *)(rbuf + r_symoff);
		for (uint32_t i = 0; i < routs.n; i++)
			sy[1 + i] = (elf_symbol){.st_info = STB_LOCAL << 4 | STT_SECTION, .st_shndx = routs.v[i].shidx};
	}
	ld_phase_time_report("-r: copy, relocations, symbols");

	/* headers */
	char *shstr = (char *)rbuf + r_shstroff;
	elf_section_header *shd = (elf_section_header *)(rbuf + shoff_r);
	for (uint32_t i = 0; i < routs.n; i++) {
		ld_relocatable_section *r = &routs.v[i];
		strcpy(shstr + r->nameoff, r->name);
		shd[r->shidx] = (elf_section_header){.sh_name = r->nameoff, .sh_type = r->type, .sh_flags = r->flags,
				       .sh_offset = r->off, .sh_size = r->size, .sh_addralign = r->align,
				       .sh_entsize = r->entsize};
		if (r->grp) {	/* sh_link the symbol table, sh_info the signature */
			const ld_input_section *gs = &isecs.v[r->grp - 1];
			const ld_object *o = objs.v[gs->obj];
			uint32_t y = o->sh[gs->shndx].sh_info;

			shd[r->shidx].sh_size = r->size;
			shd[r->shidx].sh_link = r_symidx;
			shd[r->shidx].sh_info = y >= o->firstg ? gsyms[o->gs[y - o->firstg]].outidx : lmap[gs->obj][y];
		}
		if (r->nrela) {
			sprintf(shstr + r->relanameoff, ".rela%s", r->name);
			shd[r->relaidx] = (elf_section_header){.sh_name = r->relanameoff, .sh_type = SHT_RELA,
						 .sh_flags = SHF_INFO_LINK | (r->member ? SHF_GROUP : 0),
						 .sh_offset = r->relaoff, .sh_size = r->nrela * sizeof(elf_rela),
						 .sh_link = r_symidx, .sh_info = r->shidx, .sh_addralign = 8,
						 .sh_entsize = sizeof(elf_rela)};
		}
	}
	strcpy(shstr + shs_symtab, ".symtab");
	strcpy(shstr + shs_strtab, ".strtab");
	strcpy(shstr + shs_shstrtab, ".shstrtab");
	shd[r_symidx] = (elf_section_header){.sh_name = shs_symtab, .sh_type = SHT_SYMTAB, .sh_offset = r_symoff,
			       .sh_size = r_nsyms * sizeof(elf_symbol), .sh_link = r_stridx, .sh_info = r_nlocal,
			       .sh_addralign = 8, .sh_entsize = sizeof(elf_symbol)};
	shd[r_stridx] = (elf_section_header){.sh_name = shs_strtab, .sh_type = SHT_STRTAB, .sh_offset = r_stroff,
			       .sh_size = strsz, .sh_addralign = 1};
	shd[r_shstridx] = (elf_section_header){.sh_name = shs_shstrtab, .sh_type = SHT_STRTAB, .sh_offset = r_shstroff,
				 .sh_size = r_shstrsize, .sh_addralign = 1};
	elf_file_header *eh = (elf_file_header *)rbuf;
	memcpy(eh->e_ident, "\177ELF\2\1\1", 7);
	eh->e_type = 1;
	eh->e_machine = x86_ld ? 62 : 183;
	eh->e_version = 1;
	eh->e_shoff = shoff_r;
	eh->e_ehsize = sizeof(elf_file_header);
	eh->e_shentsize = sizeof(elf_section_header);
	eh->e_shnum = r_nsh;
	eh->e_shstrndx = r_shstridx;
	if (atomic_load(&nerrors)) {
		munmap(rbuf, fsize);
		return 1;
	}
	out = rbuf;
	filesize = fsize;
	int rc = ld_output_file_write();
	ld_phase_time_report("-r: write");
	ld_region_unmap(rbuf, fsize);
	out = NULL;
	for (uint32_t i = 0; i < objs.n; i++)
		if (objs.v[i]->mapped && objs.v[i]->buf)
			ld_region_unmap(objs.v[i]->buf, objs.v[i]->size);
	ld_global_table_free();
	memset(&routs, 0, sizeof routs);
	return rc || atomic_load(&nerrors) ? 1 : 0;
}
