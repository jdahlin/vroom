/* arm64 relocation scan: which relocations are supported, which need a dynamic relocation; part of ld/ld.c (#included there) */

/* ---------------- relocation scan ---------------- */


/* ld_relocation_target_find()'s abs/undef/discarded without computing the address */
static void a64_ld_relocation_target_classify(struct ld_context *ctx, const ld_object *o, uint32_t symidx, ld_relocation_target *t)
{
	t->is_undefined = t->discarded = false;
	if (symidx >= o->first_global_index) {
		uint32_t g = o->global_slots[symidx - o->first_global_index];
		ld_global_symbol *s = &ctx->symbols.globals[g];
		if (s->state == LD_SYMBOL_STATE_UNDEFINED || s->demoted) {
			t->is_undefined = t->is_absolute = true;
			return;
		}
		t->is_absolute = ld_global_symbol_is_absolute(ctx, g);
		return;
	}
	const elf_symbol *y = &o->symbols[symidx];
	if (y->st_shndx == SHN_ABS) {
		t->is_absolute = true;
		return;
	}
	if (y->st_shndx == SHN_UNDEF || y->st_shndx >= 0xff00) {
		t->is_undefined = t->is_absolute = true;
		return;
	}
	uint32_t sec = o->first_input_section + y->st_shndx;
	uint32_t os = ld_input_section_output_section(ctx, sec);
	t->is_absolute = false;
	t->discarded = ctx->inputs.sections.v[sec].kind == LD_SECTION_KIND_NONE || os == LD_OUTPUT_SECTION_DISCARD || os == LD_NONE;
}

static void a64_ld_relocation_scan(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_input_section *is = &ctx->inputs.sections.v[i];
	if (!is->rela_count || is->kind == LD_SECTION_KIND_NONE || is->object == LD_NONE)
		return;
	uint32_t os = ld_input_section_output_section(ctx, i);
	if (os == LD_OUTPUT_SECTION_DISCARD || os == LD_NONE)
		return;
	ld_object *o = ctx->inputs.objects.v[is->object];
	uint32_t n = 0;
	ld_relocation_target t;
	for (uint32_t r = 0; r < is->rela_count; r++) {
		const elf_rela *rl = &is->rela[r];
		uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;
		if (symi >= o->symbol_count) {
			ld_error_report(ctx, "%s: bad symbol index in relocation", o->path);
			continue;
		}
		a64_ld_relocation_target_classify(ctx, o, symi, &t);
		if (symi >= o->first_global_index && (is->flags & SHF_ALLOC) && ctx->symbols.globals[o->global_slots[symi - o->first_global_index]].demoted)
			/* lld: its winning definition is in a dropped COMDAT member or /DISCARD/ */
			ld_error_report(ctx, "relocation refers to a symbol in a discarded section: %s\n>>> defined in %s\n>>> referenced by %s",
			      ld_global_name(ctx, o->global_slots[symi - o->first_global_index]), ctx->inputs.objects.v[ctx->symbols.globals[o->global_slots[symi - o->first_global_index]].object]->path, o->path);
		else if (t.discarded && (is->flags & SHF_ALLOC))
			ld_error_report(ctx, "%s:(%s+0x%llx): relocation refers to a discarded section", o->path, is->name,
			      (unsigned long long)rl->r_offset);
		switch (type) {
		case R_AARCH64_ABS64:
			if (ctx->relocations.pic && (is->flags & SHF_ALLOC) && !t.is_absolute)
				n++;
			break;
		case R_AARCH64_ABS32: case R_AARCH64_ABS16:
			if (ctx->relocations.pic && (is->flags & SHF_ALLOC) && !t.is_absolute && !t.is_undefined)
				ld_error_report(ctx, "%s:(%s+0x%llx): relocation R_AARCH64_ABS32/16 cannot be used against "
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
		case R_AARCH64_ADR_GOT_PAGE: case R_AARCH64_LD64_GOT_LO12_NC:
			if (symi < o->first_global_index)
				atomic_store_explicit(&o->local_needs_got[symi], 1, memory_order_relaxed);
			else
				atomic_store(&ctx->symbols.globals[o->global_slots[symi - o->first_global_index]].needs_got, 1);
			break;
		/* TLS: local-exec, and initial-exec/TLSDESC relaxed to it (an executable, as lld) */
		case R_AARCH64_TLSLE_ADD_TPREL_HI12: case R_AARCH64_TLSLE_ADD_TPREL_LO12:
		case R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
		case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21: case R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
		case R_AARCH64_TLSDESC_ADR_PAGE21: case R_AARCH64_TLSDESC_LD64_LO12:
		case R_AARCH64_TLSDESC_ADD_LO12: case R_AARCH64_TLSDESC_CALL:
			if (ctx->options.shared || ctx->options.relocatable)
				ld_error_report(ctx, "%s:(%s+0x%llx): TLS relocation %u in a shared object is not supported", o->path,
				      is->name, (unsigned long long)rl->r_offset, type);
			else if (t.is_undefined)
				ld_error_report(ctx, "%s:(%s+0x%llx): TLS relocation against an undefined symbol", o->path, is->name,
				      (unsigned long long)rl->r_offset);
			break;
		default:
			ld_error_report(ctx, "%s:(%s+0x%llx): unsupported relocation type %u", o->path, is->name,
			      (unsigned long long)rl->r_offset, type);
		}
	}
	is->dynamic_relocation_count = n;
}
