/* The undefined-symbol diagnostics: spelling suggestions, reference locations, the report. */

/* lld's getAlternativeSpelling: a defined global (or a local of the referencing object) one edit
   away, tried in lld's order: substitute, transpose, insert, delete (characters '0'..'z') */
static const char *suggest_one(const char *cand, Obj *ref, const char **where)
{
	uint32_t g = gfind(cand);

	if (g != NONE && gsyms[g].state == GS_OBJ && gsyms[g].obj != NONE) {
		*where = objs.v[gsyms[g].obj]->path;
		return gname(g);
	}
	(void)ref;	/* (lld suggested no local of the referencing object in our tests) */
	return NULL;
}

static const char *suggest(const char *name, Obj *ref, const char **where)
{
	size_t n = strlen(name);
	char *b = malloc(n + 2);
	const char *r = NULL;

	if (!b)
		return NULL;
	memcpy(b, name, n + 1);
	for (size_t i = 0; i < n && !r; i++) {
		char old = b[i];

		for (char c = '0'; c <= 'z' && !r; c++) {
			b[i] = c;
			r = suggest_one(b, ref, where);
		}
		b[i] = old;
	}
	for (size_t i = 0; i + 1 < n && !r; i++) {
		char t = b[i];

		b[i] = b[i + 1], b[i + 1] = t;
		r = suggest_one(b, ref, where);
		b[i + 1] = b[i], b[i] = t;
	}
	b[0] = '0';
	memcpy(b + 1, name, n + 1);
	for (size_t i = 0; i <= n && !r; i++) {
		for (char c = '0'; c <= 'z' && !r; c++) {
			b[i] = c;
			r = suggest_one(b, ref, where);
		}
		if (i < n) {	/* (lld: std::swap(newName[i], newName[i + 1])) */
			char t = b[i];

			b[i] = b[i + 1];
			b[i + 1] = t;
		}
	}
	for (size_t i = 0; i < n && !r; i++) {
		memcpy(b, name, i);
		memcpy(b + i, name + i + 1, n - i);
		r = suggest_one(b, ref, where);
	}
	free(b);
	return r;
}

/* lld's location of a reference: "SRC.c\n>>>               OBJ:(FUNC)" (the object's STT_FILE, the
   defined symbol around the offset, else SECTION+0xOFF; an archive member "M.o:(F) in archive A") */
static void ref_loc(vbuf *b, Obj *o, uint32_t sec, uint64_t off)
{
	const char *src = NULL, *path = o->path, *lp = strrchr(path, '('), *fn = NULL;
	char obj[4096];

	for (uint32_t k = 1; k < o->firstg && !src; k++)
		if ((o->sym[k].st_info & 15) == STT_FILE)
			src = o->str + o->sym[k].st_name;
	for (uint32_t k = 1; k < o->nsym && !fn; k++) {
		const Sym *y = &o->sym[k];
		uint8_t t = y->st_info & 15;

		if (y->st_shndx == sec && t != STT_SECTION && t != STT_FILE && o->str[y->st_name] &&
		    o->str[y->st_name] != '$' && y->st_value <= off && off < y->st_value + y->st_size)
			fn = o->str + y->st_name;
	}
	if (lp && path[strlen(path) - 1] == ')')	/* "A(M)" */
		snprintf(obj, sizeof obj, "%.*s", (int)(strlen(lp + 1) - 1), lp + 1);
	else
		snprintf(obj, sizeof obj, "%s", path);
	if (src)
		vbuf_f(b, "%s\n>>>               ", src);
	if (fn)
		vbuf_f(b, "%s:(%s)", obj, fn);
	else
		vbuf_f(b, "%s:(%s+0x%llx)", obj, o->shstr + o->sh[sec].sh_name, (unsigned long long)off);
	if (lp && path[strlen(path) - 1] == ')')
		vbuf_f(b, " in archive %.*s", (int)(lp - path), path);
}

/* "undefined symbol: X" and every reference to it (lld: the first 3, then "referenced N more times") */
static void undef_refs(uint32_t gid, Obj *first, bool spell)
{
	vbuf b = {0};
	uint32_t nref = 0;
	Obj *reffile = NULL;

	vbuf_f(&b, "undefined symbol: %s", gname(gid));
	for (uint32_t oi = 0; oi < objs.n; oi++) {
		Obj *o = objs.v[oi];

		for (uint32_t k = 1; k < o->nsh; k++) {
			const Shdr *h = &o->sh[k];

			if (h->sh_type != SHT_RELA || h->sh_info >= o->nsh || !(o->sh[h->sh_info].sh_flags & SHF_ALLOC))
				continue;
			const Rela *r = (const Rela *)(o->buf + h->sh_offset);
			for (uint64_t j = 0; j < h->sh_size / sizeof(Rela); j++) {
				uint32_t si = (uint32_t)(r[j].r_info >> 32);

				if (si < o->firstg || si >= o->nsym || o->gs[si - o->firstg] != gid)
					continue;
				if (!reffile)
					reffile = o;
				if (nref++ < 3) {
					vbuf_f(&b, "\n>>> referenced by ");
					ref_loc(&b, o, h->sh_info, r[j].r_offset);
				}
			}
		}
	}
	if (!nref)
		vbuf_f(&b, "\n>>> referenced by %s", first->path);
	else if (nref > 3)
		vbuf_f(&b, "\n>>> referenced %u more times", nref - 3);
	{
		const char *where = NULL, *alt = spell ? suggest(gname(gid), reffile, &where) : NULL;

		if (alt)
			vbuf_f(&b, "\n>>> did you mean: %s\n>>> defined in: %s", alt, where);
	}
	vbuf_put(&b, "", 1);
	error("%s", (const char *)b.p);
	free(b.p);
}

static void report_undefined(void)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		G *s = &gsyms[gorder.v[i]];
		if (s->state != GS_UNDEF || s->bind == STB_WEAK)
			continue;
		if ((kget(&s->first) >> 32) >= objs.n)	/* referenced only by the script */
			continue;
		/* x86-64 executables relax every GD/LD sequence to LE, and its call
		   to __tls_get_addr goes with it (lld scans no relocation there) */
		if (x86_ld && !opt.shared && !strcmp(gname(gorder.v[i]), "__tls_get_addr"))
			continue;
		if (opt.no_undefined || !opt.shared) {
			undef_refs(gorder.v[i], objs.v[kget(&s->first) >> 32], n < 2);	/* (lld: spelling for the first two) */
			n++;
		}
	}
	(void)n;
}
