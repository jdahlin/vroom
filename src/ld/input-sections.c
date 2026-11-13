/* Turning an object's section headers into input sections, and dropping .note.gnu.property. */

static bool is_content(const Shdr *s)
{
	switch (s->sh_type) {
	case SHT_PROGBITS: case SHT_NOBITS: case SHT_NOTE: case SHT_INIT_ARRAY:
	case SHT_FINI_ARRAY: case SHT_PREINIT_ARRAY:
	case 0x70000001: /* SHT_X86_64_UNWIND: x86-64's .eh_frame */
		return true;
	default:
		return false;
	}
}

static void fill_isecs(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	if (!o->buf)
		return;
	for (uint32_t s = 0; s < o->nsh; s++) {
		const Shdr *h = &o->sh[s];
		ISec *is = &isecs.v[o->isec0 + s];
		is->obj = o->idx;
		is->shndx = s;
		is->osec = NONE;
		is->isd = NONE;
		is->aux = NONE;
		is->name = o->shstr + h->sh_name;
		if (h->sh_type == SHT_RELA) {
			if (h->sh_info < o->nsh) {
				ISec *t = &isecs.v[o->isec0 + h->sh_info];
				t->rela = (const Rela *)(o->buf + h->sh_offset);
				t->nrela = h->sh_size / sizeof(Rela);
			}
			continue;
		}
		if (h->sh_type == SHT_REL) {
			error("%s: SHT_REL not supported", o->path);
			continue;
		}
		if (!is_content(h) || (h->sh_flags & SHF_EXCLUDE && !opt.relocatable))
			continue;
		if (opt.strip_debug && !(h->sh_flags & SHF_ALLOC) && !strncmp(is->name, ".debug", 6))
			continue;
		is->kind = K_NORMAL;
		is->data = h->sh_type == SHT_NOBITS ? NULL : o->buf + h->sh_offset;
		is->size = h->sh_size;
		is->flags = h->sh_flags & ~(uint64_t)SHF_GROUP;
		is->type = h->sh_type;
		is->align = h->sh_addralign ? h->sh_addralign : 1;
		is->entsize = h->sh_entsize;
		if (!opt.relocatable && (h->sh_flags & SHF_MERGE) && h->sh_size && h->sh_entsize) {
			if (h->sh_size % h->sh_entsize)
				error("%s:(%s): SHF_MERGE section size must be a multiple of sh_entsize", o->path, is->name);
			if (h->sh_flags & SHF_WRITE)
				error("%s:(%s): writable SHF_MERGE section is not supported", o->path, is->name);
			is->kind = K_MERGE_IN;
		}
	}
}

/* .note.gnu.property: lld keeps one synthetic note holding the AND of every
 * input's FEATURE_1_AND (an input without the note counts as 0).  Only the
 * common case is done: when the AND is 0 every input note is dropped, as lld
 * emits none; otherwise the inputs stay as they are (TODO: the merged note) */
static void drop_gnu_property(void)
{
	const uint32_t want = x86_ld ? 0xc0000002u : 0xc0000000u;	/* X86 / AARCH64 FEATURE_1_AND */
	uint32_t and = ~0u;

	if (opt.relocatable)
		return;
	for (uint32_t i = 0; i < objs.n && and; i++) {
		Obj *o = objs.v[i];
		uint32_t f = 0;

		for (uint32_t s = 0; o->buf && s < o->nsh; s++) {
			const Shdr *h = &o->sh[s];

			if (h->sh_type != SHT_NOTE || strcmp(isecs.v[o->isec0 + s].name, ".note.gnu.property"))
				continue;
			const uint8_t *p = o->buf + h->sh_offset, *e = p + h->sh_size;
			while (e - p >= 16) {
				uint32_t nn, dn, ty;
				memcpy(&nn, p, 4); memcpy(&dn, p + 4, 4); memcpy(&ty, p + 8, 4);
				const uint8_t *d = p + 12 + ((nn + 3) & ~3u), *de = d + dn;
				if (de > e || d > e)
					break;
				for (const uint8_t *q = d; ty == 5 && de - q >= 8;) {
					uint32_t pt, pz;
					memcpy(&pt, q, 4); memcpy(&pz, q + 4, 4);
					if (pt == want && pz >= 4 && de - q >= 12)
						memcpy(&f, q + 8, 4);
					q += 8 + ((pz + 7) & ~7u);
				}
				p = d + ((dn + 7) & ~7u);
			}
		}
		and &= f;
	}
	if (and)
		return;
	for (uint32_t i = 0; i < objs.n; i++) {
		Obj *o = objs.v[i];

		for (uint32_t s = 0; o->buf && s < o->nsh; s++)
			if (o->sh[s].sh_type == SHT_NOTE && !strcmp(isecs.v[o->isec0 + s].name, ".note.gnu.property"))
				isecs.v[o->isec0 + s].kind = K_NONE;
	}
}
