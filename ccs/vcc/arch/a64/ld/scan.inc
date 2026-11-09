/* arm64 relocation scan: which relocations are supported, which need a dynamic relocation; part of ld/ld.c (#included there) */

/* ---------------- relocation scan ---------------- */

static bool pic;

/* reloc_target()'s abs/undef/discarded without computing the address */
static void reloc_class(const Obj *o, uint32_t symidx, Target *t)
{
	t->undef = t->discarded = false;
	if (symidx >= o->firstg) {
		uint32_t g = o->gs[symidx - o->firstg];
		G *s = &gsyms[g];
		if (s->state == GS_UNDEF || s->demoted) {
			t->undef = t->abs = true;
			return;
		}
		t->abs = gsym_isabs(g);
		return;
	}
	const Sym *y = &o->sym[symidx];
	if (y->st_shndx == SHN_ABS) {
		t->abs = true;
		return;
	}
	if (y->st_shndx == SHN_UNDEF || y->st_shndx >= 0xff00) {
		t->undef = t->abs = true;
		return;
	}
	uint32_t sec = o->isec0 + y->st_shndx;
	uint32_t os = isec_osec(sec);
	t->abs = false;
	t->discarded = isecs.v[sec].kind == K_NONE || os == OS_DISCARD || os == NONE;
}

static void scan_relocs(void *arg, size_t i)
{
	(void)arg;
	ISec *is = &isecs.v[i];
	if (!is->nrela || is->kind == K_NONE || is->obj == NONE)
		return;
	uint32_t os = isec_osec(i);
	if (os == OS_DISCARD || os == NONE)
		return;
	Obj *o = objs.v[is->obj];
	uint32_t n = 0;
	Target t;
	for (uint32_t r = 0; r < is->nrela; r++) {
		const Rela *rl = &is->rela[r];
		uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;
		if (symi >= o->nsym) {
			error("%s: bad symbol index in relocation", o->path);
			continue;
		}
		reloc_class(o, symi, &t);
		if (t.discarded && (is->flags & SHF_ALLOC))
			error("%s:(%s+0x%llx): relocation refers to a discarded section", o->path, is->name,
			      (unsigned long long)rl->r_offset);
		switch (type) {
		case R_AARCH64_ABS64:
			if (pic && (is->flags & SHF_ALLOC) && !t.abs)
				n++;
			break;
		case R_AARCH64_ABS32: case R_AARCH64_ABS16:
			if (pic && (is->flags & SHF_ALLOC) && !t.abs && !t.undef)
				error("%s:(%s+0x%llx): relocation R_AARCH64_ABS32/16 cannot be used against "
				      "a non-absolute symbol; recompile with -fPIC", o->path, is->name,
				      (unsigned long long)rl->r_offset);
			break;
		case R_AARCH64_NONE: case R_AARCH64_PREL64: case R_AARCH64_PREL32: case R_AARCH64_PREL16:
		case R_AARCH64_CALL26: case R_AARCH64_JUMP26: case R_AARCH64_ADR_PREL_PG_HI21:
		case R_AARCH64_ADR_PREL_LO21: case R_AARCH64_ADD_ABS_LO12_NC:
		case R_AARCH64_LDST8_ABS_LO12_NC: case R_AARCH64_LDST16_ABS_LO12_NC:
		case R_AARCH64_LDST32_ABS_LO12_NC: case R_AARCH64_LDST64_ABS_LO12_NC:
		case R_AARCH64_LDST128_ABS_LO12_NC: case R_AARCH64_LD_PREL_LO19:
			break;
		default:
			error("%s:(%s+0x%llx): unsupported relocation type %u", o->path, is->name,
			      (unsigned long long)rl->r_offset, type);
		}
	}
	is->ndyn = n;
}
