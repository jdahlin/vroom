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
} ld_relocation_target;

static void ld_relocation_target_find(const ld_object *o, uint32_t symidx, int64_t addend, ld_relocation_target *t)
{
	t->undef = t->discarded = false;
	if (symidx >= o->firstg) {
		uint32_t g = o->gs[symidx - o->firstg];
		ld_global_symbol *s = &ctx->symbols.globals[g];
		t->key = g;
		t->global = true;
		if (s->state == LD_SYMBOL_STATE_UNDEFINED || s->demoted) {
			t->undef = true;
			t->abs = true;
			t->s = 0;
			t->va = addend;
			return;
		}
		t->abs = ld_global_symbol_is_absolute(g);
		t->s = ld_global_symbol_address(g);
		if (s->state == LD_SYMBOL_STATE_OBJECT && s->sec != LD_NONE) {
			ld_input_section *is = &ctx->inputs.sections.v[s->sec];
			if (is->kind == LD_SECTION_KIND_MERGE_INPUT) {
				t->va = ld_input_section_address(s->sec, s->val + addend) ;
				/* non-section symbol: piece of value, then + addend */
				t->va = ld_input_section_address(s->sec, s->val) + addend;
				return;
			}
		}
		t->va = t->s + addend;
		return;
	}
	const elf_symbol *y = &o->sym[symidx];
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
	ld_input_section *is = &ctx->inputs.sections.v[sec];
	t->abs = false;
	uint32_t os = ld_input_section_output_section(sec);
	if (is->kind == LD_SECTION_KIND_NONE || os == LD_OUTPUT_SECTION_DISCARD || os == LD_NONE) {
		t->discarded = true;
		t->s = 0;
		t->va = addend;
		return;
	}
	if ((y->st_info & 15) == STT_SECTION) {
		t->s = ld_input_section_address(sec, 0);
		t->va = ld_input_section_address(sec, (uint64_t)addend);
		if (is->kind != LD_SECTION_KIND_MERGE_INPUT)
			t->va = t->s + addend;
		return;
	}
	t->s = ld_input_section_address(sec, y->st_value);
	t->va = t->s + addend;
}

/*
 * Final addresses, once layout is done: ld_relocation_target_find() for the copy pass
 * from per-symbol and per-section values computed once (fin_*), falling
 * back to ld_relocation_target_find() for undefined, discarded and merge-piece targets.
 */

static void ld_input_section_final_address_set(void *arg, size_t i)
{
	(void)arg;
	ld_input_section *is = &ctx->inputs.sections.v[i];
	uint32_t os = ld_input_section_output_section(i);
	ctx->relocations.final_section_addresses[i] = is->kind == LD_SECTION_KIND_NONE || is->kind == LD_SECTION_KIND_MERGE_INPUT || os == LD_NONE || os == LD_OUTPUT_SECTION_DISCARD
			      ? UINT64_MAX : ctx->script.sections.v[os].va + is->off;
}
static void ld_global_symbol_final_address_set(void *arg, size_t g)
{
	(void)arg;
	ld_global_symbol *s = &ctx->symbols.globals[g];
	s->fundef = s->state == LD_SYMBOL_STATE_UNDEFINED || s->demoted;
	if (!s->fundef) {
		s->fabs = ld_global_symbol_is_absolute((uint32_t)g);
		s->fva = ld_global_symbol_address((uint32_t)g);
	}
}
static void ld_final_addresses_compute(void)
{
	ctx->relocations.final_section_addresses = ld_region_alloc((size_t)ctx->inputs.sections.n * sizeof(uint64_t));
	ld_parallel_for(ctx->inputs.sections.n, ld_input_section_final_address_set, NULL);
	ld_parallel_for(atomic_load(&ctx->symbols.count), ld_global_symbol_final_address_set, NULL);
}

static inline void ld_relocation_target_final_find(const ld_object *o, uint32_t symidx, int64_t addend, ld_relocation_target *t)
{
	if (symidx >= o->firstg) {
		uint32_t g = o->gs[symidx - o->firstg];
		const ld_global_symbol *s = &ctx->symbols.globals[g];
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
		const elf_symbol *y = &o->sym[symidx];
		if (y->st_shndx != SHN_UNDEF && y->st_shndx < 0xff00) {
			uint64_t b = ctx->relocations.final_section_addresses[o->isec0 + y->st_shndx];
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
	ld_relocation_target_find(o, symidx, addend, t);
}
