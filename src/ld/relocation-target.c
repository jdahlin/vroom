/* Where a relocation points: the target symbol or section, merge-aware, before and after final addresses. */

/* ---------------- relocation targets ---------------- */

typedef struct {
	uint64_t va;			/* S + A, merge-aware */
	uint64_t s;			/* S (without addend; for relaxation) */
	bool abs;			/* target is absolute (no dynamic relocation) */
	bool undef;
	bool discarded;
	uint32_t key;			/* identity for ADRP relaxation */
	bool global;
} Target;

static void reloc_target(const Obj *o, uint32_t symidx, int64_t addend, Target *t)
{
	t->undef = t->discarded = false;
	if (symidx >= o->firstg) {
		uint32_t g = o->gs[symidx - o->firstg];
		G *s = &gsyms[g];
		t->key = g;
		t->global = true;
		if (s->state == GS_UNDEF || s->demoted) {
			t->undef = true;
			t->abs = true;
			t->s = 0;
			t->va = addend;
			return;
		}
		t->abs = gsym_isabs(g);
		t->s = gsym_va(g);
		if (s->state == GS_OBJ && s->sec != NONE) {
			ISec *is = &isecs.v[s->sec];
			if (is->kind == K_MERGE_IN) {
				t->va = isec_va(s->sec, s->val + addend) ;
				/* non-section symbol: piece of value, then + addend */
				t->va = isec_va(s->sec, s->val) + addend;
				return;
			}
		}
		t->va = t->s + addend;
		return;
	}
	const Sym *y = &o->sym[symidx];
	t->key = symidx;
	t->global = false;
	if (y->st_shndx == SHN_ABS) {
		t->abs = true;
		t->s = y->st_value;
		t->va = t->s + addend;
		return;
	}
	if (y->st_shndx == SHN_UNDEF || y->st_shndx >= 0xff00) {
		t->undef = true;
		t->abs = true;
		t->s = 0;
		t->va = addend;
		return;
	}
	uint32_t sec = o->isec0 + y->st_shndx;
	ISec *is = &isecs.v[sec];
	t->abs = false;
	uint32_t os = isec_osec(sec);
	if (is->kind == K_NONE || os == OS_DISCARD || os == NONE) {
		t->discarded = true;
		t->s = 0;
		t->va = addend;
		return;
	}
	if ((y->st_info & 15) == STT_SECTION) {
		t->s = isec_va(sec, 0);
		t->va = isec_va(sec, (uint64_t)addend);
		if (is->kind != K_MERGE_IN)
			t->va = t->s + addend;
		return;
	}
	t->s = isec_va(sec, y->st_value);
	t->va = t->s + addend;
}

/*
 * Final addresses, once layout is done: reloc_target() for the copy pass
 * from per-symbol and per-section values computed once (fin_*), falling
 * back to reloc_target() for undefined, discarded and merge-piece targets.
 */
static uint64_t *isec_fva;		/* isec_va(s, 0), or UINT64_MAX: take reloc_target */

static void fin_isec(void *arg, size_t i)
{
	(void)arg;
	ISec *is = &isecs.v[i];
	uint32_t os = isec_osec(i);
	isec_fva[i] = is->kind == K_NONE || is->kind == K_MERGE_IN || os == NONE || os == OS_DISCARD
			      ? UINT64_MAX : osecs.v[os].va + is->off;
}
static void fin_gsym(void *arg, size_t g)
{
	(void)arg;
	G *s = &gsyms[g];
	s->fundef = s->state == GS_UNDEF || s->demoted;
	if (!s->fundef) {
		s->fabs = gsym_isabs((uint32_t)g);
		s->fva = gsym_va((uint32_t)g);
	}
}
static void final_addresses(void)
{
	isec_fva = alloc((size_t)isecs.n * sizeof(uint64_t));
	pfor(isecs.n, fin_isec, NULL);
	pfor(atomic_load(&ngsyms), fin_gsym, NULL);
}

static inline void reloc_target_final(const Obj *o, uint32_t symidx, int64_t addend, Target *t)
{
	if (symidx >= o->firstg) {
		uint32_t g = o->gs[symidx - o->firstg];
		const G *s = &gsyms[g];
		if (!s->fundef) {
			t->undef = t->discarded = false;
			t->key = g;
			t->global = true;
			t->abs = s->fabs;
			t->s = s->fva;
			t->va = s->fva + addend;
			return;
		}
	} else {
		const Sym *y = &o->sym[symidx];
		if (y->st_shndx != SHN_UNDEF && y->st_shndx < 0xff00) {
			uint64_t b = isec_fva[o->isec0 + y->st_shndx];
			if (b != UINT64_MAX) {
				t->undef = t->discarded = false;
				t->key = symidx;
				t->global = false;
				t->abs = false;
				t->s = b + ((y->st_info & 15) == STT_SECTION ? 0 : y->st_value);
				t->va = t->s + addend;
				return;
			}
		}
	}
	reloc_target(o, symidx, addend, t);
}
