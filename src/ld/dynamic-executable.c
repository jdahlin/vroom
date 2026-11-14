/* x86-64 dynamic executable sections, imports, PLT, and default layout. */

#include "ld-internal.h"

VPRIV uint32_t xstr_add(struct ld_context *ctx, const char *str)
{
	size_t n = strlen(str);

	for (uint32_t o = 1; o + n < ctx->dyn.xstr.n; ) {	/* (few strings: a linear search, lld dedups too) */
		size_t l = strlen(ctx->dyn.xstr.v + o);

		if (l == n && !memcmp(ctx->dyn.xstr.v + o, str, n))
			return o;
		o += l + 1;
	}
	uint32_t o = ctx->dyn.xstr.n;

	for (size_t i = 0; i <= n; i++)
		VVEC_PUSH(ctx->dyn.xstr, str[i], ld_region_alloc);
	return o;
}

VPRIV const elf_symbol *g_dsosym(struct ld_context *ctx, const ld_global_symbol *s)
{
	return &ctx->dso.dsos.v[s->dso1 - 1]->dsym[s->dsym];
}


/* ---- the sections, created with the other synthetic ones (sized in dyn_after_scan) ---- */

VPRIV void dyn_add_sections(struct ld_context *ctx)
{
	if (!ctx->dyn.dynexe)
		return;
	if (ctx->options.dynlinker && !ctx->options.no_dynlinker) {
		size_t n = strlen(ctx->options.dynlinker) + 1;
		uint8_t *d = ld_region_alloc(n);

		memcpy(d, ctx->options.dynlinker, n);
		ctx->dyn.interp_isec = ld_input_section_create(ctx, ".interp", LD_SECTION_KIND_INTERPRETER, SHT_PROGBITS, SHF_ALLOC, 1);
		ctx->inputs.sections.v[ctx->dyn.interp_isec].data = d;
		ctx->inputs.sections.v[ctx->dyn.interp_isec].size = n;
	}
	ctx->output.dynsym_section = ld_input_section_create(ctx, ".dynsym", LD_SECTION_KIND_DYNAMIC_SYMBOLS, SHT_DYNSYM, SHF_ALLOC, 8);
	ctx->inputs.sections.v[ctx->output.dynsym_section].entry_size = sizeof(elf_symbol);
	ctx->output.versym_section = ld_input_section_create(ctx, ".gnu.version", LD_SECTION_KIND_SYMBOL_VERSIONS, 0x6fffffffu /* SHT_GNU_versym */, SHF_ALLOC, 2);
	ctx->inputs.sections.v[ctx->output.versym_section].entry_size = 2;
	ctx->dyn.verneed_isec = ld_input_section_create(ctx, ".gnu.version_r", LD_SECTION_KIND_VERSION_REQUIREMENTS, 0x6ffffffeu, SHF_ALLOC, 4);
	ctx->output.gnu_hash_section = ld_input_section_create(ctx, ".gnu.hash", LD_SECTION_KIND_GNU_HASH_TABLE, 0x6ffffff6u /* SHT_GNU_HASH */, SHF_ALLOC, 8);
	ctx->output.hash_section = ld_input_section_create(ctx, ".hash", LD_SECTION_KIND_HASH_TABLE, SHT_HASH, SHF_ALLOC, 4);
	ctx->inputs.sections.v[ctx->output.hash_section].entry_size = 4;
	ctx->output.dynstr_section = ld_input_section_create(ctx, ".dynstr", LD_SECTION_KIND_DYNAMIC_STRINGS, SHT_STRTAB, SHF_ALLOC, 1);
	ctx->output.rela_dyn_section = ld_input_section_create(ctx, ".rela.dyn", LD_SECTION_KIND_DYNAMIC_RELOCATIONS, SHT_RELA, SHF_ALLOC, 8);
	ctx->inputs.sections.v[ctx->output.rela_dyn_section].entry_size = sizeof(elf_rela);
	ctx->dyn.relaplt_isec = ld_input_section_create(ctx, ".rela.plt", LD_SECTION_KIND_PLT_RELOCATIONS, SHT_RELA, SHF_ALLOC, 8);
	ctx->inputs.sections.v[ctx->dyn.relaplt_isec].entry_size = sizeof(elf_rela);
	ctx->dyn.plt_isec = ld_input_section_create(ctx, ".plt", LD_SECTION_KIND_PLT, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16);
	ctx->output.dynamic_section = ld_input_section_create(ctx, ".dynamic", LD_SECTION_KIND_DYNAMIC, SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE, 8);
	ctx->inputs.sections.v[ctx->output.dynamic_section].entry_size = 16;
	ctx->dyn.copy_isec = ld_input_section_create(ctx, ".bss", LD_SECTION_KIND_NORMAL, SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 1);
}

/* .got.plt: dynamic executables, and _GLOBAL_OFFSET_TABLE_ in a static one */
VPRIV void dyn_add_gotplt(struct ld_context *ctx)
{
	uint32_t g = ctx->inputs.x86 && !ctx->options.relocatable ? ld_global_name_find(ctx, "_GLOBAL_OFFSET_TABLE_") : LD_NONE;

	if (ctx->dyn.dynexe || (g != LD_NONE && ld_global_symbol_is_referenced(ctx, g) && ctx->symbols.globals[g].state == LD_SYMBOL_STATE_UNDEFINED))
		ctx->dyn.gotplt_isec = ld_input_section_create(ctx, ".got.plt", LD_SECTION_KIND_GOT_PLT, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 8);
}

/* ---- linker-defined symbols ---- */

static const char *const reserved_names[] = {
	"__ehdr_start", "_GLOBAL_OFFSET_TABLE_", "_DYNAMIC",
	"__preinit_array_start", "__preinit_array_end", "__init_array_start", "__init_array_end",
	"__fini_array_start", "__fini_array_end", "__rela_iplt_start", "__rela_iplt_end",
	"_etext", "etext", "_edata", "edata", "__bss_start", "_end", "end",
};



VPRIV void define_reserved(struct ld_context *ctx)
{
	for (uint32_t i = 0; i < NRESERVED; i++) {
		ctx->dyn.reserved_g[i] = LD_NONE;
		if (!ctx->inputs.x86 || ctx->options.relocatable)
			continue;
		if (i == 0 && !ctx->dyn.deflayout)
			continue;	/* (the headers are loaded only in the default layout) */
		if (i == 2 && !ctx->dyn.dynexe)
			continue;
		uint32_t g = ld_global_name_find(ctx, reserved_names[i]);

		if (g == LD_NONE || !ld_global_symbol_is_referenced(ctx, g) || ctx->symbols.globals[g].state != LD_SYMBOL_STATE_UNDEFINED)
			continue;
		ld_global_symbol *s = &ctx->symbols.globals[g];

		s->state = LD_SYMBOL_STATE_SCRIPT;
		s->dso1 = 0;
		s->object = LD_NONE;
		s->expression_value = (ld_expression_value){.kind = LD_VALUE_KIND_OUTPUT_SECTION, .index = LD_OUTPUT_SECTION_AETHER, .value = 0, .alignment = 1};	/* (not absolute; set after layout) */
		s->type = STT_NOTYPE;
		s->size = 0;
		s->binding = STB_GLOBAL;
		if (i <= 10)
			s->visibility = STV_HIDDEN;
		s->thunk = LD_NONE;
		s->version = 1;
		ctx->dyn.reserved_g[i] = g;
	}
}

VPRIV uint32_t osec_named(struct ld_context *ctx, const char *n)
{
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++)
		if (!strcmp(ctx->script.sections.v[ctx->layout.output_sections.v[i]].name, n))
			return ctx->layout.output_sections.v[i];
	return LD_NONE;
}

/* after the final layout: their values, relative to the first allocated section */
VPRIV void patch_reserved(struct ld_context *ctx)
{
	uint32_t first = LD_NONE;
	uint64_t etext = 0, edata = 0, end = 0, bss = 0;
	bool have_bss = false;

	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		ld_output_section *os = &ctx->script.sections.v[ctx->layout.output_sections.v[i]];

		if (!(os->flags & SHF_ALLOC))
			continue;
		if (first == LD_NONE)
			first = ctx->layout.output_sections.v[i];
		bool tbss = (os->flags & SHF_TLS) && os->type == SHT_NOBITS;

		if (os->flags & SHF_EXECINSTR)
			etext = os->address + os->size;
		if (os->type != SHT_NOBITS)
			edata = os->address + os->size;
		if (!tbss && os->address + os->size > end)
			end = os->address + os->size;
		if (os->type == SHT_NOBITS && !tbss && !have_bss && !strcmp(os->name, ".bss"))
			bss = os->address, have_bss = true;
	}
	if (first == LD_NONE)
		return;
	uint64_t base = ctx->script.sections.v[first].address;

	for (uint32_t i = 0; i < NRESERVED; i++) {
		if (ctx->dyn.reserved_g[i] == LD_NONE)
			continue;
		uint64_t v = base;
		const char *n = reserved_names[i];

		if (i == 0)
			v = ctx->layout.file_header_address;
		else if (i == 1)
			v = ctx->dyn.gotplt_isec != LD_NONE && ld_input_section_is_live(ctx, ctx->dyn.gotplt_isec) ? ld_input_section_address(ctx, ctx->dyn.gotplt_isec, 0) :
			    ctx->output.got_section != LD_NONE && ld_input_section_is_live(ctx, ctx->output.got_section) ? ld_input_section_address(ctx, ctx->output.got_section, 0) : base;
		else if (i == 2)
			v = ld_input_section_address(ctx, ctx->output.dynamic_section, 0);
		else if (i <= 8) {
			char sn[32];
			const char *u = strchr(n + 2, '_');	/* "__init_array_start": ".init_array" */
			size_t l = (size_t)(strrchr(n, '_') - (n + 2));

			(void)u;
			snprintf(sn, sizeof sn, ".%.*s", (int)l, n + 2);
			uint32_t o = osec_named(ctx, sn);

			if (o != LD_NONE)
				v = ctx->script.sections.v[o].address + (strstr(n, "_end") ? ctx->script.sections.v[o].size : 0);
		} else if (i <= 10)
			v = base;	/* no .rela.iplt: empty */
		else if (i <= 12)
			v = etext;
		else if (i <= 14)
			v = edata;
		else if (i == 15)
			v = have_bss ? bss : edata;
		else
			v = end;
		ctx->symbols.globals[ctx->dyn.reserved_g[i]].expression_value = (ld_expression_value){.kind = LD_VALUE_KIND_OUTPUT_SECTION, .index = first, .value = v - base, .alignment = 1};
	}
}

/* ---- the scan: what a relocation against an imported symbol needs ---- */

/* the target of a relocation, if it is an imported symbol */
VPRIV ld_global_symbol *dyn_imported(struct ld_context *ctx, const ld_object *o, uint32_t symi)
{
	if (!ctx->dyn.dynexe || symi < o->first_global_index)
		return NULL;
	ld_global_symbol *s = &ctx->symbols.globals[o->global_slots[symi - o->first_global_index]];

	return s->dso1 ? s : NULL;
}

VPRIV void reloc_where(struct ld_context *ctx, const ld_object *o, const ld_input_section *is, const elf_rela *rl, char *buf, size_t n)
{
	(void)ctx;
	snprintf(buf, n, "%s:(%s+0x%llx)", o->path, is->name, (unsigned long long)rl->r_offset);
}

/* one relocation of the x86-64 scan; returns the dynamic relocations it needs in .rela.dyn */
VPRIV uint32_t dyn_scan_reloc(struct ld_context *ctx, const ld_object *o, const ld_input_section *is, const elf_rela *rl, const ld_relocation_target *t)
{
	uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;
	ld_global_symbol *s = dyn_imported(ctx, o, symi);
	bool wr = is->flags & SHF_WRITE;
	char where[512];

	if (!(is->flags & SHF_ALLOC))
		return 0;
	if (s) {
		bool func = s->type == STT_FUNC || s->type == 10 /* STT_GNU_IFUNC */;

		switch (type) {
		case R_X86_64_PLT32:
			if (func || s->type == STT_NOTYPE)
				atomic_store_explicit(&s->needplt, 1, memory_order_relaxed);
			else
				atomic_store_explicit(&s->needcopy, 1, memory_order_relaxed);
			return 0;
		case R_X86_64_64:
			if (wr)
				return 1;	/* symbolic */
			__attribute__((fallthrough));
		case R_X86_64_PC32: case R_X86_64_PC64: case R_X86_64_32: case R_X86_64_32S:
		case R_X86_64_16: case R_X86_64_PC16: case R_X86_64_8: case R_X86_64_PC8:
			if (func) {
				atomic_store_explicit(&s->needplt, 1, memory_order_relaxed);
				atomic_store_explicit(&s->canon, 1, memory_order_relaxed);
			} else
				atomic_store_explicit(&s->needcopy, 1, memory_order_relaxed);
			return 0;
		case R_X86_64_GOTTPOFF: case R_X86_64_TLSGD: case R_X86_64_TLSLD: case R_X86_64_DTPOFF32:
		case R_X86_64_DTPOFF64: case R_X86_64_TPOFF32: case R_X86_64_TPOFF64:
			reloc_where(ctx, o, is, rl, where, sizeof where);
			ld_error_report(ctx, "%s: TLS relocation %u against %s, defined in a shared object, is not supported",
			      where, type, ld_global_name(ctx, o->global_slots[symi - o->first_global_index]));
			return 0;
		default:
			return 0;
		}
	}
	if (!ctx->relocations.pic || t->is_absolute || t->is_undefined || t->discarded)
		return 0;
	if (type == R_X86_64_64) {
		if (wr)
			return 1;	/* R_X86_64_RELATIVE */
		reloc_where(ctx, o, is, rl, where, sizeof where);
		ld_error_report(ctx, "%s: relocation R_X86_64_64 cannot be used in a read-only section of a PIE; recompile with -fPIC", where);
	} else if (type == R_X86_64_32 || type == R_X86_64_32S) {
		reloc_where(ctx, o, is, rl, where, sizeof where);
		ld_error_report(ctx, "%s: relocation R_X86_64_32%s cannot be used against a local address in a PIE; recompile with -fPIC",
		      where, type == R_X86_64_32S ? "S" : "");
	}
	return 0;
}

/* in the relocation pass: R_X86_64_64's dynamic relocation (the count dyn_scan_reloc gave) */
VPRIV void dyn_reloc64(struct ld_context *ctx, const ld_object *o, const ld_input_section *is, const elf_rela *rl, const ld_relocation_target *t, uint64_t P, elf_rela *dyn, uint32_t *nd)
{
	uint32_t symi = rl->r_info >> 32;
	ld_global_symbol *s = dyn_imported(ctx, o, symi);

	if (!dyn || !(is->flags & SHF_ALLOC) || !(is->flags & SHF_WRITE))
		return;
	if (s) {
		dyn[*nd].r_offset = P;
		dyn[*nd].r_info = (uint64_t)s->dynamic_index << 32 | R_X86_64_64;
		dyn[*nd].r_addend = rl->r_addend;
		(*nd)++;
		return;
	}
	if (!ctx->relocations.pic || t->is_absolute || t->is_undefined || t->discarded)
		return;
	dyn[*nd].r_offset = P;
	dyn[*nd].r_info = R_X86_64_RELATIVE;
	dyn[*nd].r_addend = (int64_t)t->address;
	(*nd)++;
}

/* the address a call or reference to an imported symbol resolves to (its PLT entry), or 0 */
VPRIV uint64_t dyn_plt_va(struct ld_context *ctx, const ld_global_symbol *s)
{
	return s->needplt ? ld_input_section_address(ctx, ctx->dyn.plt_isec, 16 * ((uint64_t)s->pltidx + 1)) : 0;
}

/* ---- after the scan: PLT, copies, .dynsym, versions, sizes ---- */

static int cmp_xdsym(const void *a, const void *b, void *arg)
{
	struct ld_context *ctx = arg;
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	uint32_t hx = ld_symbol_name_gnu_hash(ld_global_name(ctx, x)) % ctx->dyn.xgh_nb, hy = ld_symbol_name_gnu_hash(ld_global_name(ctx, y)) % ctx->dyn.xgh_nb;

	return hx != hy ? (hx < hy ? -1 : 1) : (x > y) - (x < y);
}

static void dyn_symbols_sort(struct ld_context *ctx, uint32_t first, uint32_t n)
{
    /* Stable bottom-up merge sort: hash buckets then global id. */
    uint32_t *a = ctx->dyn.xdsyms.v + first;
    uint32_t *tmp = ld_region_alloc((size_t)n * sizeof *tmp);
    for (uint64_t width = 1; width < n; width *= 2) {
        for (uint64_t lo = 0; lo < n; lo += width * 2) {
            uint64_t mid = lo + width < n ? lo + width : n;
            uint64_t hi = lo + width * 2 < n ? lo + width * 2 : n;
            uint64_t i = lo, j = mid, k = lo;
            while (i < mid || j < hi)
                tmp[k++] = j == hi || (i < mid && cmp_xdsym(&a[i], &a[j], ctx) <= 0) ? a[i++] : a[j++];
        }
        memcpy(a, tmp, (size_t)n * sizeof *a);
    }
}

VPRIV void drop_isec(struct ld_context *ctx, uint32_t si)
{
	if (si == LD_NONE || !ld_input_section_is_live(ctx, si))
		return;
	ld_input_section_description *d = &ctx->script.input_descriptions.v[ctx->inputs.sections.v[si].input_description];
	uint32_t w = 0;

	for (uint32_t k = 0; k < d->sections.n; k++)
		if (d->sections.v[k] != si)
			d->sections.v[w++] = d->sections.v[k];
	d->sections.n = w;
	ctx->inputs.sections.v[si].output_section = LD_NONE;
}

/* the imported symbols that became local definitions (copies, canonical PLTs): before the .got is counted */
VPRIV void dyn_localize(struct ld_context *ctx)
{
	if (!ctx->dyn.dynexe)
		return;
	uint32_t ng = atomic_load(&ctx->symbols.count);
	uint64_t coff = 0;
	uint32_t calign = 1;

	/* (in symbol table order: ids depend on the threads' interning order) */
	ctx->dyn.plts.n = 0;
	for (uint32_t i = 0; i < ctx->symbols.order.n; i++) {
		uint32_t g = ctx->symbols.order.v[i];
		ld_global_symbol *s = &ctx->symbols.globals[g];

		if (!s->dso1 || s->state != LD_SYMBOL_STATE_UNDEFINED)
			continue;
		if (s->needplt) {
			s->pltidx = ctx->dyn.plts.n;
			VVEC_PUSH(ctx->dyn.plts, g, ld_region_alloc);
		}
	}
	for (uint32_t i = 0, n0 = ctx->symbols.order.n; i < n0; i++) {
		uint32_t g = ctx->symbols.order.v[i];
		ld_global_symbol *s = &ctx->symbols.globals[g];

		if (!s->dso1 || s->state != LD_SYMBOL_STATE_UNDEFINED)
			continue;
		const elf_symbol *y = g_dsosym(ctx, s);

		if (s->canon) {
			s->state = LD_SYMBOL_STATE_SCRIPT;
			s->expression_value = (ld_expression_value){.kind = LD_VALUE_KIND_INPUT_SECTION, .index = ctx->dyn.plt_isec, .value = 16 * ((uint64_t)s->pltidx + 1), .alignment = 1};
			s->type = STT_FUNC;
		} else if (s->needcopy) {
			if (!y->st_size)
				ld_warning_report(ctx, "copy relocation of %s, which has no size", ld_global_name(ctx, g));
			uint64_t v = y->st_value;
			uint32_t a = v ? 1u << __builtin_ctzll(v) : 16;

			if (a > 32)
				a = 32;
			coff = ld_value_align_up(coff, a);
			if (a > calign)
				calign = a;
			/* the same object under other names (environ, __environ): one copy, all of them
			 * exported (the shared object's own references bind to the copy) */
			{
				const Dso *so = ctx->dso.dsos.v[s->dso1 - 1];

				for (uint32_t k = 1; k < so->ndsym; k++) {
					const elf_symbol *z = &so->dsym[k];

					if (k == s->dsym || z->st_value != v || z->st_shndx != y->st_shndx || !ld_dso_sym_def(so, k) ||
					    (z->st_info & 15) == STT_FUNC)
						continue;
					const char *an = so->dstr + z->st_name;
					uint32_t h = ld_global_name_find(ctx, an);

					if (h != LD_NONE && (ctx->symbols.globals[h].state != LD_SYMBOL_STATE_UNDEFINED || ctx->symbols.globals[h].dso1))
						continue;	/* defined here, or imported: handled below */
					if (h == LD_NONE) {
						h = ld_global_name_intern(ctx, an);
						VVEC_PUSH(ctx->symbols.order, h, ld_region_alloc);
						ng = atomic_load(&ctx->symbols.count);
					}
					ld_global_symbol *q = &ctx->symbols.globals[h];

					q->dso1 = s->dso1;
					q->dsym = k;
					q->state = LD_SYMBOL_STATE_SCRIPT;
					q->expression_value = (ld_expression_value){.kind = LD_VALUE_KIND_INPUT_SECTION, .index = ctx->dyn.copy_isec, .value = coff, .alignment = 1};
					q->needcopy = 2;
					q->type = z->st_info & 15;
					q->size = z->st_size;
					q->binding = z->st_info >> 4 == STB_WEAK ? STB_WEAK : STB_GLOBAL;
					q->visibility = STV_DEFAULT;
					q->object = LD_NONE;
					q->thunk = LD_NONE;
					q->version = 1;
				}
			}
			for (uint32_t h = 0; h < ng; h++) {
				ld_global_symbol *q = &ctx->symbols.globals[h];

				if (h == g || q->dso1 != s->dso1 || q->state != LD_SYMBOL_STATE_UNDEFINED || q->canon)
					continue;
				const elf_symbol *z = g_dsosym(ctx, q);

				if (z->st_value == v && z->st_shndx == y->st_shndx && (z->st_info & 15) != STT_FUNC) {
					q->state = LD_SYMBOL_STATE_SCRIPT;
					q->expression_value = (ld_expression_value){.kind = LD_VALUE_KIND_INPUT_SECTION, .index = ctx->dyn.copy_isec, .value = coff, .alignment = 1};
					q->needcopy = 2;	/* an alias: no relocation of its own */
					q->binding = z->st_info >> 4 == STB_WEAK ? STB_WEAK : STB_GLOBAL;
					q->visibility = STV_DEFAULT;
					q->object = LD_NONE;
				}
			}
			s->state = LD_SYMBOL_STATE_SCRIPT;
			s->expression_value = (ld_expression_value){.kind = LD_VALUE_KIND_INPUT_SECTION, .index = ctx->dyn.copy_isec, .value = coff, .alignment = 1};
			s->needcopy = 1;
			coff += y->st_size;
		} else
			continue;
		s->binding = y->st_info >> 4 == STB_WEAK ? STB_WEAK : STB_GLOBAL;
		s->visibility = STV_DEFAULT;
		s->object = LD_NONE;
	}
	ctx->inputs.sections.v[ctx->dyn.copy_isec].size = coff;
	ctx->inputs.sections.v[ctx->dyn.copy_isec].alignment = calign;
}

VPRIV void dyn_after_scan(struct ld_context *ctx)
{
	if (!ctx->dyn.dynexe)
		return;
	uint32_t ng = atomic_load(&ctx->symbols.count);

	/* exported definitions: -E, or named by a shared object's undefined symbol */
	ctx->dyn.xexport = ld_region_alloc(ng + 1);
	memset(ctx->dyn.xexport, 0, ng + 1);
	for (uint32_t d = 0; d < ctx->dso.dsos.n; d++) {
		const Dso *so = ctx->dso.dsos.v[d];

		for (uint32_t k = 1; k < so->ndsym; k++)
			if (so->dsym[k].st_shndx == SHN_UNDEF) {
				uint32_t g = ld_global_name_find(ctx, so->dstr + so->dsym[k].st_name);

				if (g != LD_NONE && ctx->symbols.globals[g].state == LD_SYMBOL_STATE_OBJECT && !ctx->symbols.globals[g].demoted)
					ctx->dyn.xexport[g] = 1;
			}
	}
	/* .dynsym: imported (undefined), then the hashed definitions by .gnu.hash bucket */
	ctx->dyn.xdsyms.n = 0;
	uint32_t nund = 0;

	for (uint32_t i = 0; i < ctx->symbols.order.n; i++) {
		uint32_t g = ctx->symbols.order.v[i];
		ld_global_symbol *s = &ctx->symbols.globals[g];

		if (s->dso1 && s->state == LD_SYMBOL_STATE_UNDEFINED && ld_global_symbol_is_referenced(ctx, g))
			VVEC_PUSH(ctx->dyn.xdsyms, g, ld_region_alloc);
	}
	nund = ctx->dyn.xdsyms.n;
	for (uint32_t i = 0; i < ctx->symbols.order.n; i++) {
		uint32_t g = ctx->symbols.order.v[i];
		ld_global_symbol *s = &ctx->symbols.globals[g];
		bool def = (s->dso1 && s->state == LD_SYMBOL_STATE_SCRIPT) ||
			   (s->state == LD_SYMBOL_STATE_OBJECT && !s->demoted && (ctx->dyn.xexport[g] || ctx->options.export_dynamic) &&
			    (s->visibility == STV_DEFAULT || s->visibility == STV_PROTECTED) && s->binding != STB_LOCAL && s->version != 0);

		if (def)
			VVEC_PUSH(ctx->dyn.xdsyms, g, ld_region_alloc);
	}
	uint32_t n = ctx->dyn.xdsyms.n, nh = n - nund;

	ctx->dyn.xgh_nb = nh / 4 > 1 ? nh / 4 : 1;
	ctx->dyn.xgh_mw = 1;
	while (ctx->dyn.xgh_mw <= (uint64_t)nh * 12 / 64)
		ctx->dyn.xgh_mw <<= 1;
	if (nh)
		dyn_symbols_sort(ctx, nund, nh);
	ctx->dyn.xsymndx = nund + 1;
	ctx->dyn.xstr.n = 0;
	VVEC_PUSH(ctx->dyn.xstr, 0, ld_region_alloc);
	ctx->dyn.xname = ld_region_alloc((n + 1) * sizeof(uint32_t));
	ctx->dyn.xhash = ld_region_alloc((n + 1) * sizeof(uint32_t));
	ctx->dyn.xver = ld_region_alloc((n + 1) * sizeof(uint16_t));
	/* DT_NEEDED: every shared object, an --as-ctx->dyn.needed one only when used */
	ctx->dyn.needed.n = 0;
	for (uint32_t d = 0; d < ctx->dso.dsos.n; d++)
		if (!ctx->dso.dsos.v[d]->as_needed || atomic_load(&ctx->dso.dsos.v[d]->used))
			VVEC_PUSH(ctx->dyn.needed, xstr_add(ctx, ctx->dso.dsos.v[d]->soname), ld_region_alloc);
	ctx->dyn.needvers.n = 0;
	ctx->dyn.nverneed = 0;
	uint16_t nextver = 2;

	for (uint32_t i = 0; i < n; i++) {
		uint32_t g = ctx->dyn.xdsyms.v[i];
		ld_global_symbol *s = &ctx->symbols.globals[g];

		s->dynamic_index = i + 1;
		ctx->dyn.xname[i] = xstr_add(ctx, ld_global_name(ctx, g));
		ctx->dyn.xhash[i] = ld_symbol_name_gnu_hash(ld_global_name(ctx, g));
		ctx->dyn.xver[i] = 1;
		if (!s->dso1)
			continue;
		const Dso *so = ctx->dso.dsos.v[s->dso1 - 1];
		const char *vn = ld_dso_vername(so, s->dsym);

		if (!vn)
			continue;
		/* one Vernaux per (file, version) */
		uint32_t k;

		for (k = 0; k < ctx->dyn.needvers.n; k++)
			if (ctx->dyn.needvers.v[k].dso == s->dso1 - 1 && !strcmp(ctx->dyn.xstr.v + ctx->dyn.needvers.v[k].name, vn))
				break;
		if (k == ctx->dyn.needvers.n) {
			NeedVer nv = { .dso = s->dso1 - 1, .name = xstr_add(ctx, vn), .hash = ld_symbol_name_elf_hash(vn), .other = nextver++ };
			uint32_t at = ctx->dyn.needvers.n;

			VVEC_PUSH(ctx->dyn.needvers, nv, ld_region_alloc);
			/* keep the entries of one file together (sorted insertion by dso) */
			while (at > 0 && ctx->dyn.needvers.v[at - 1].dso > nv.dso) {
				ctx->dyn.needvers.v[at] = ctx->dyn.needvers.v[at - 1];
				at--;
			}
			ctx->dyn.needvers.v[at] = nv;
			k = at;
		}
		ctx->dyn.xver[i] = ctx->dyn.needvers.v[k].other;
	}
	for (uint32_t k = 0; k < ctx->dyn.needvers.n; k++)
		if (!k || ctx->dyn.needvers.v[k].dso != ctx->dyn.needvers.v[k - 1].dso) {
			ctx->dyn.nverneed++;
			(void)xstr_add(ctx, ctx->dso.dsos.v[ctx->dyn.needvers.v[k].dso]->soname);
		}
	/* .dynamic */
	bool init = ld_global_name_find(ctx, "_init") != LD_NONE && ctx->symbols.globals[ld_global_name_find(ctx, "_init")].state == LD_SYMBOL_STATE_OBJECT;
	bool fini = ld_global_name_find(ctx, "_fini") != LD_NONE && ctx->symbols.globals[ld_global_name_find(ctx, "_fini")].state == LD_SYMBOL_STATE_OBJECT;

	ctx->dyn.nxdyn = ctx->dyn.needed.n + 1 /* DEBUG */ + 3 /* RELA */ + 4 /* PLT */ + 4 /* SYMTAB.. */ + 2 /* hashes */ + 3 /* versions */ +
		(init ? 1 : 0) + (fini ? 1 : 0) + 6 /* (pre)init/fini arrays */ + 2 /* FLAGS, FLAGS_1 */ + 1 /* NULL */;

	ctx->inputs.sections.v[ctx->output.dynsym_section].size = (uint64_t)(n + 1) * sizeof(elf_symbol);
	ctx->inputs.sections.v[ctx->output.versym_section].size = (uint64_t)(n + 1) * 2;
	ctx->inputs.sections.v[ctx->dyn.verneed_isec].size = (uint64_t)ctx->dyn.nverneed * 16 + (uint64_t)ctx->dyn.needvers.n * 16;
	ctx->inputs.sections.v[ctx->output.hash_section].size = (uint64_t)(2 + 2 * (n + 1)) * 4;
	ctx->inputs.sections.v[ctx->output.gnu_hash_section].size = 16 + (uint64_t)ctx->dyn.xgh_mw * 8 + (uint64_t)ctx->dyn.xgh_nb * 4 + (uint64_t)nh * 4;
	ctx->inputs.sections.v[ctx->output.dynstr_section].size = ctx->dyn.xstr.n;
	ctx->inputs.sections.v[ctx->dyn.relaplt_isec].size = (uint64_t)ctx->dyn.plts.n * sizeof(elf_rela);
	ctx->inputs.sections.v[ctx->dyn.plt_isec].size = ctx->dyn.plts.n ? 16 * ((uint64_t)ctx->dyn.plts.n + 1) : 0;
	ctx->inputs.sections.v[ctx->dyn.gotplt_isec].size = 8 * (3 + (uint64_t)ctx->dyn.plts.n);
	ctx->inputs.sections.v[ctx->output.dynamic_section].size = (uint64_t)ctx->dyn.nxdyn * 16;
	/* copy relocations go in .rela.dyn with the copy section's address */
	uint32_t nc = 0;

	for (uint32_t i = 0; i < n; i++)
		if (ctx->symbols.globals[ctx->dyn.xdsyms.v[i]].needcopy == 1 && ctx->symbols.globals[ctx->dyn.xdsyms.v[i]].state == LD_SYMBOL_STATE_SCRIPT)
			nc++;
	ctx->inputs.sections.v[ctx->dyn.copy_isec].dynamic_relocation_count = nc;
	if (!ctx->dyn.plts.n) {
		drop_isec(ctx, ctx->dyn.plt_isec);
		drop_isec(ctx, ctx->dyn.relaplt_isec);
	}
	if (!ctx->dyn.needvers.n)
		drop_isec(ctx, ctx->dyn.verneed_isec);
	if (!ctx->inputs.sections.v[ctx->dyn.copy_isec].size)
		drop_isec(ctx, ctx->dyn.copy_isec);
}

/* sh_link / sh_info of the dynamic sections */
VPRIV void dyn_shdrs(struct ld_context *ctx)
{
	if (!ctx->dyn.dynexe)
		return;
	uint32_t ds = ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.dynsym_section)].section_header_index, dstr = ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.dynstr_section)].section_header_index;

	ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.dynsym_section)].link = dstr;
	ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.dynsym_section)].info = 1;
	ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.hash_section)].link = ds;
	ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.gnu_hash_section)].link = ds;
	ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.versym_section)].link = ds;
	if (ld_input_section_is_live(ctx, ctx->dyn.verneed_isec)) {
		ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->dyn.verneed_isec)].link = dstr;
		ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->dyn.verneed_isec)].info = ctx->dyn.nverneed;
	}
	ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.dynamic_section)].link = dstr;
	if (ld_input_section_is_live(ctx, ctx->output.rela_dyn_section))
		ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.rela_dyn_section)].link = ds;
	if (ld_input_section_is_live(ctx, ctx->dyn.relaplt_isec)) {
		ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->dyn.relaplt_isec)].link = ds;
		ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->dyn.relaplt_isec)].info = ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->dyn.gotplt_isec)].section_header_index;
		ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->dyn.relaplt_isec)].flags |= 0x40;	/* SHF_INFO_LINK */
	}
}

/* ---- writing ---- */

VPRIV void dyn_write_gotplt(struct ld_context *ctx)
{
	if (ctx->dyn.gotplt_isec == LD_NONE || !ld_input_section_is_live(ctx, ctx->dyn.gotplt_isec))
		return;
	uint8_t *gp = isec_out(ctx, ctx->dyn.gotplt_isec);

	if (ctx->dyn.dynexe)
		ld_u64_store(gp, ld_input_section_address(ctx, ctx->output.dynamic_section, 0));
	if (!ctx->dyn.dynexe || !ctx->dyn.plts.n)
		return;
	uint64_t gva = ld_input_section_address(ctx, ctx->dyn.gotplt_isec, 0), pva = ld_input_section_address(ctx, ctx->dyn.plt_isec, 0);
	uint8_t *p = isec_out(ctx, ctx->dyn.plt_isec);
	elf_rela *jr = (elf_rela *)(void *)isec_out(ctx, ctx->dyn.relaplt_isec);

	/* PLT0: pushq GOTPLT+8(%rip); jmp *GOTPLT+16(%rip); nopl 0(%rax) */
	p[0] = 0xff, p[1] = 0x35;
	ld_u32_store(p + 2, (uint32_t)(gva + 8 - (pva + 6)));
	p[6] = 0xff, p[7] = 0x25;
	ld_u32_store(p + 8, (uint32_t)(gva + 16 - (pva + 12)));
	memcpy(p + 12, "\x0f\x1f\x40\x00", 4);
	for (uint32_t i = 0; i < ctx->dyn.plts.n; i++) {
		uint8_t *e = p + 16 * (i + 1);
		uint64_t eva = pva + 16 * ((uint64_t)i + 1), slot = gva + 8 * (3 + (uint64_t)i);

		e[0] = 0xff, e[1] = 0x25;	/* jmp *slot(%rip) */
		ld_u32_store(e + 2, (uint32_t)(slot - (eva + 6)));
		e[6] = 0x68;			/* pushq $i */
		ld_u32_store(e + 7, i);
		e[11] = 0xe9;			/* jmp PLT0 */
		ld_u32_store(e + 12, (uint32_t)(pva - (eva + 16)));
		ld_u64_store(gp + 8 * (3 + i), eva + 6);	/* lazy: back to the push (ld.so adds the load bias) */
		jr[i].r_offset = slot;
		jr[i].r_info = (uint64_t)ctx->symbols.globals[ctx->dyn.plts.v[i]].dynamic_index << 32 | R_X86_64_JUMP_SLOT;
		jr[i].r_addend = 0;
	}
}

VPRIV void dyn_write(struct ld_context *ctx)
{
	dyn_write_gotplt(ctx);
	if (!ctx->dyn.dynexe)
		return;
	uint32_t n = ctx->dyn.xdsyms.n;
	elf_symbol *ds = (elf_symbol *)(void *)isec_out(ctx, ctx->output.dynsym_section);

	if (ctx->dyn.interp_isec != LD_NONE && ld_input_section_is_live(ctx, ctx->dyn.interp_isec))
		memcpy(isec_out(ctx, ctx->dyn.interp_isec), ctx->inputs.sections.v[ctx->dyn.interp_isec].data, ctx->inputs.sections.v[ctx->dyn.interp_isec].size);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t g = ctx->dyn.xdsyms.v[i];
		ld_global_symbol *s = &ctx->symbols.globals[g];
		elf_symbol *d = &ds[i + 1];

		d->st_name = ctx->dyn.xname[i];
		d->st_other = s->state == LD_SYMBOL_STATE_UNDEFINED ? STV_DEFAULT : s->visibility;
		if (s->state == LD_SYMBOL_STATE_UNDEFINED) {
			d->st_info = (uint8_t)(s->binding << 4 | s->type);
			continue;
		}
		d->st_info = (uint8_t)(s->binding << 4 | (s->type == 10 /* STT_GNU_IFUNC */ ? STT_FUNC : s->type));
		d->st_value = ld_global_symbol_address(ctx, g);
		if (s->dso1 && s->canon)
			d->st_shndx = SHN_UNDEF;	/* canonical PLT: undefined, with the entry's address */
		else if (s->state == LD_SYMBOL_STATE_SCRIPT)
			d->st_shndx = ld_symbol_section_index_of_input_section(ctx, s->expression_value.index);
		else
			d->st_shndx = s->input_section == LD_NONE ? SHN_ABS : ld_symbol_section_index_of_input_section(ctx, s->input_section);
		d->st_size = s->dso1 ? g_dsosym(ctx, s)->st_size : s->size;
	}
	memcpy(isec_out(ctx, ctx->output.dynstr_section), ctx->dyn.xstr.v, ctx->dyn.xstr.n);
	uint8_t *vs = isec_out(ctx, ctx->output.versym_section);

	for (uint32_t i = 0; i < n; i++)
		memcpy(vs + 2 * (i + 1), &ctx->dyn.xver[i], 2);
	if (ctx->dyn.needvers.n) {
		uint8_t *vn = isec_out(ctx, ctx->dyn.verneed_isec);
		uint32_t at = 0;

		for (uint32_t k = 0; k < ctx->dyn.needvers.n; ) {
			uint32_t d = ctx->dyn.needvers.v[k].dso, c = 0;

			while (k + c < ctx->dyn.needvers.n && ctx->dyn.needvers.v[k + c].dso == d)
				c++;
			uint8_t *e = vn + at;
			uint16_t ver = 1, cnt = (uint16_t)c;
			bool last = k + c == ctx->dyn.needvers.n;

			memcpy(e, &ver, 2);
			memcpy(e + 2, &cnt, 2);
			ld_u32_store(e + 4, xstr_add(ctx, ctx->dso.dsos.v[d]->soname));
			ld_u32_store(e + 8, 16);
			ld_u32_store(e + 12, last ? 0 : 16 + 16 * c);
			for (uint32_t j = 0; j < c; j++) {
				uint8_t *a = e + 16 + 16 * j;
				uint16_t fl = 0, oth = ctx->dyn.needvers.v[k + j].other;

				ld_u32_store(a, ctx->dyn.needvers.v[k + j].hash);
				memcpy(a + 4, &fl, 2);
				memcpy(a + 6, &oth, 2);
				ld_u32_store(a + 8, ctx->dyn.needvers.v[k + j].name);
				ld_u32_store(a + 12, j + 1 == c ? 0 : 16);
			}
			at += 16 + 16 * c;
			k += c;
		}
	}
	/* .hash */
	{
		uint8_t *h = isec_out(ctx, ctx->output.hash_section);
		uint32_t nb = n + 1, *bk = (uint32_t *)(void *)(h + 8), *ch = bk + nb;

		ld_u32_store(h, nb);
		ld_u32_store(h + 4, nb);
		for (uint32_t i = 0; i < n; i++) {
			uint32_t b = ld_symbol_name_elf_hash(ld_global_name(ctx, ctx->dyn.xdsyms.v[i])) % nb;

			ch[i + 1] = bk[b];
			bk[b] = i + 1;
		}
	}
	/* .gnu.hash */
	{
		uint8_t *gh = isec_out(ctx, ctx->output.gnu_hash_section);
		uint64_t *bloom = (uint64_t *)(void *)(gh + 16);
		uint32_t *gb = (uint32_t *)(void *)(gh + 16 + ctx->dyn.xgh_mw * 8), *gv = gb + ctx->dyn.xgh_nb, old = LD_NONE;

		ld_u32_store(gh, ctx->dyn.xgh_nb);
		ld_u32_store(gh + 4, ctx->dyn.xsymndx);
		ld_u32_store(gh + 8, ctx->dyn.xgh_mw);
		ld_u32_store(gh + 12, 26);
		for (uint32_t i = ctx->dyn.xsymndx - 1; i < n; i++) {
			uint32_t hv = ctx->dyn.xhash[i], b = hv % ctx->dyn.xgh_nb;
			bool last = i + 1 == n || ctx->dyn.xhash[i + 1] % ctx->dyn.xgh_nb != b;

			bloom[(hv / 64) & (ctx->dyn.xgh_mw - 1)] |= (uint64_t)1 << (hv % 64) | (uint64_t)1 << ((hv >> 26) % 64);
			*gv++ = last ? hv | 1 : hv & ~1u;
			if (b != old) {
				gb[b] = i + 1;
				old = b;
			}
		}
	}
	/* copy relocations */
	if (ctx->dyn.copy_isec != LD_NONE && ctx->inputs.sections.v[ctx->dyn.copy_isec].dynamic_relocation_count && ctx->relocations.rela_dyn_buffer) {
		elf_rela *r = ctx->relocations.rela_dyn_buffer + ctx->inputs.sections.v[ctx->dyn.copy_isec].first_dynamic_relocation;
		uint32_t k = 0;

		for (uint32_t i = 0; i < n; i++) {
			ld_global_symbol *s = &ctx->symbols.globals[ctx->dyn.xdsyms.v[i]];

			if (s->needcopy != 1 || s->state != LD_SYMBOL_STATE_SCRIPT)
				continue;
			r[k].r_offset = ld_global_symbol_address(ctx, ctx->dyn.xdsyms.v[i]);
			r[k].r_info = (uint64_t)s->dynamic_index << 32 | R_X86_64_COPY;
			r[k].r_addend = 0;
			k++;
		}
	}
	/* .dynamic */
	uint8_t *dy = isec_out(ctx, ctx->output.dynamic_section);
	uint32_t at = 0;
#define XDYN(tag, v) do { ld_u64_store(dy + 16 * at, (uint64_t)(tag)); ld_u64_store(dy + 16 * at + 8, (uint64_t)(v)); at++; } while (0)
	for (uint32_t k = 0; k < ctx->dyn.needed.n; k++)
		XDYN(1, ctx->dyn.needed.v[k]);				/* DT_NEEDED */
	XDYN(21, 0);						/* DT_DEBUG */
	if (ld_input_section_is_live(ctx, ctx->output.rela_dyn_section) && ctx->inputs.sections.v[ctx->output.rela_dyn_section].size) {
		XDYN(7, ld_input_section_address(ctx, ctx->output.rela_dyn_section, 0));		/* DT_RELA */
		XDYN(8, ctx->inputs.sections.v[ctx->output.rela_dyn_section].size);		/* DT_RELASZ */
		XDYN(9, sizeof(elf_rela));				/* DT_RELAENT */
	}
	if (ctx->dyn.plts.n) {
		XDYN(23, ld_input_section_address(ctx, ctx->dyn.relaplt_isec, 0));		/* DT_JMPREL */
		XDYN(2, ctx->inputs.sections.v[ctx->dyn.relaplt_isec].size);		/* DT_PLTRELSZ */
		XDYN(3, ld_input_section_address(ctx, ctx->dyn.gotplt_isec, 0));		/* DT_PLTGOT */
		XDYN(20, 7);					/* DT_PLTREL: DT_RELA */
	}
	XDYN(6, ld_input_section_address(ctx, ctx->output.dynsym_section, 0));			/* DT_SYMTAB */
	XDYN(11, sizeof(elf_symbol));					/* DT_SYMENT */
	XDYN(5, ld_input_section_address(ctx, ctx->output.dynstr_section, 0));			/* DT_STRTAB */
	XDYN(10, ctx->dyn.xstr.n);					/* DT_STRSZ */
	XDYN(0x6ffffef5, /* DT_GNU_HASH */ ld_input_section_address(ctx, ctx->output.gnu_hash_section, 0));
	XDYN(4, ld_input_section_address(ctx, ctx->output.hash_section, 0));				/* DT_HASH */
	XDYN(0x6ffffff0, /* DT_VERSYM */ ld_input_section_address(ctx, ctx->output.versym_section, 0));
	if (ctx->dyn.needvers.n) {
		XDYN(0x6ffffffe, ld_input_section_address(ctx, ctx->dyn.verneed_isec, 0));	/* DT_VERNEED */
		XDYN(0x6fffffff, ctx->dyn.nverneed);			/* DT_VERNEEDNUM */
	}
	uint32_t g;

	if ((g = ld_global_name_find(ctx, "_init")) != LD_NONE && ctx->symbols.globals[g].state == LD_SYMBOL_STATE_OBJECT)
		XDYN(12, ld_global_symbol_address(ctx, g));				/* DT_INIT */
	if ((g = ld_global_name_find(ctx, "_fini")) != LD_NONE && ctx->symbols.globals[g].state == LD_SYMBOL_STATE_OBJECT)
		XDYN(13, ld_global_symbol_address(ctx, g));				/* DT_FINI */
	static const struct { const char *name; uint32_t tag, sz; } arr[] = {
		{".preinit_array", 32, 33}, {".init_array", 25, 27}, {".fini_array", 26, 28},
	};
	for (uint32_t k = 0; k < 3; k++) {
		uint32_t o = osec_named(ctx, arr[k].name);

		if (o != LD_NONE && ctx->script.sections.v[o].size) {
			XDYN(arr[k].tag, ctx->script.sections.v[o].address);
			XDYN(arr[k].sz, ctx->script.sections.v[o].size);
		}
	}
	if (ctx->options.znow)
		XDYN(30, 8);					/* DT_FLAGS: DF_BIND_NOW */
	if (ctx->options.pie || ctx->options.znow)
		XDYN(0x6ffffffb, (ctx->options.znow ? 1u : 0) | (ctx->options.pie ? 0x08000000u : 0));	/* DT_FLAGS_1: NOW, PIE */
	XDYN(0, 0);
#undef XDYN
}

/* ---- the default layout ---- */





/* lld fixSectionAlignments: a section that starts a PT_LOAD is advanced to the
 * next page at the same page offset (and PT_TLS's start to its alignment) */
VPRIV uint64_t deflayout_dot(struct ld_context *ctx, uint32_t oi, uint64_t d)
{
	ld_output_section *os = &ctx->script.sections.v[oi];
	uint64_t pg = ctx->options.max_page_size;

	if (ctx->dyn.dl_prev != LD_NONE) {
		ld_output_section *pv = &ctx->script.sections.v[ctx->dyn.dl_prev];
		bool split = ld_output_section_segment_flags(pv) != ld_output_section_segment_flags(os) || (!ctx->options.no_relro && ld_output_section_is_relro(ctx, ctx->dyn.dl_prev) && !ld_output_section_is_relro(ctx, oi));

		if (split) {
			if (os->flags & SHF_TLS)
				d = ld_value_align_up(d, pg) + ld_value_align_up(d % pg, os->address_alignment);
			else
				d = ld_value_align_up(d, pg) + d % pg;
		}
	}
	ctx->dyn.dl_prev = oi;
	return d;
}

VPRIV uint8_t *isec_out(struct ld_context *ctx, uint32_t si)
{
 uint32_t oi = ld_input_section_output_section(ctx, si);
 return ctx->output.image + ctx->script.sections.v[oi].offset + ctx->inputs.sections.v[si].offset;
}
