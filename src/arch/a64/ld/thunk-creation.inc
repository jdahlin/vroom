/* arm64 thunk pass: calls out of branch range get thunks, as lld does; part of ld/ld.c (#included there) */

/* ---------------- thunk pass ---------------- */

/* call relocations of an ld_input_section_description in thunk-pass order, with the destination
 * resolved to (plain input section, offset) where the layout cannot change
 * which section it is; anything else (script symbols, merge sections,
 * undefined, absolute, discarded) takes ld_relocation_target_find() every pass */
typedef struct a64_ld_call { uint32_t input_section, relocation_index, destination_section, thunk; uint64_t destination_offset; } a64_ld_call;
static void a64_ld_thunk_calls_collect(struct ld_context *ctx, ld_input_section_description *d)
{
	uint32_t n = 0;
	for (uint32_t s = 0; s < d->sections.n; s++) {
		ld_input_section *is = &ctx->inputs.sections.v[d->sections.v[s]];
		if (is->object == LD_NONE)
			continue;
		for (uint32_t r = 0; r < is->rela_count; r++) {
			uint32_t type = (uint32_t)is->rela[r].r_info;
			n += type == R_AARCH64_CALL26 || type == R_AARCH64_JUMP26;
		}
	}
	d->calls = ld_region_alloc((size_t)n * sizeof(a64_ld_call) + 1);
	d->call_count = 0;
	for (uint32_t s = 0; s < d->sections.n; s++) {
		uint32_t si = d->sections.v[s];
		ld_input_section *is = &ctx->inputs.sections.v[si];
		if (is->object == LD_NONE)
			continue;
		ld_object *o = ctx->inputs.objects.v[is->object];
		for (uint32_t r = 0; r < is->rela_count; r++) {
			const elf_rela *rl = &is->rela[r];
			uint32_t type = (uint32_t)rl->r_info;
			if (type != R_AARCH64_CALL26 && type != R_AARCH64_JUMP26)
				continue;
			a64_ld_call *c = &d->calls[d->call_count++];
			c->input_section = si;
			c->relocation_index = r;
			c->destination_section = LD_NONE;
			c->thunk = LD_NONE;		/* a64_ld_thunk_relocation_find(si, r), kept in step with a64_ld_thunk_relocation_record */
			uint32_t symi = rl->r_info >> 32;
			uint32_t sec = LD_NONE;
			uint64_t val = 0;
			if (symi >= o->first_global_index) {
				ld_global_symbol *g = &ctx->symbols.globals[o->global_slots[symi - o->first_global_index]];
				if (g->state == LD_SYMBOL_STATE_OBJECT && !g->demoted && g->input_section != LD_NONE) {
					sec = g->input_section;
					val = g->value;
				}
			} else {
				const elf_symbol *y = &o->symbols[symi];
				if (y->st_shndx != SHN_UNDEF && y->st_shndx < 0xff00) {
					sec = o->first_input_section + y->st_shndx;
					val = (y->st_info & 15) == STT_SECTION ? 0 : y->st_value;
				}
			}
			if (sec == LD_NONE)
				continue;
			ld_input_section *ds = &ctx->inputs.sections.v[sec];
			if (ds->kind != LD_SECTION_KIND_NORMAL || ds->output_section == LD_NONE || ds->output_section == LD_OUTPUT_SECTION_DISCARD)
				continue;
			c->destination_section = sec;
			c->destination_offset = val + (uint64_t)rl->r_addend;
		}
	}
}

static bool a64_ld_thunks_create(struct ld_context *ctx, int pass)
{
	bool changed = false;
	ld_relocation_target t;
	for (uint32_t c = 0; c < ctx->script.commands.n; c++) {
		if (ctx->script.commands.v[c].kind != LD_COMMAND_OUTPUT_SECTION)
			continue;
		uint32_t oi = ctx->script.commands.v[c].index;
		ld_output_section *os = &ctx->script.sections.v[oi];
		if (!os->live || (os->flags & (SHF_ALLOC | SHF_EXECINSTR)) != (SHF_ALLOC | SHF_EXECINSTR))
			continue;
		for (uint32_t k = 0; k < os->commands.n; k++) {
			if (os->commands.v[k].kind != LD_COMMAND_INPUT_SECTION_DESCRIPTION)
				continue;
			uint32_t di = os->commands.v[k].index;
			ld_input_section_description *d = &ctx->script.input_descriptions.v[di];
			if (pass == 0 && d->sections.n) {
				ld_input_section *last = &ctx->inputs.sections.v[d->sections.v[d->sections.n - 1]];
				d->thunk_offset = last->offset + last->size;
			}
			if (pass == 0)
				a64_ld_thunk_calls_collect(ctx, d);
			uint32_t cur = LD_NONE;
			uint64_t base = 0;
			for (uint32_t ci = 0; ci < d->call_count; ci++) {
				a64_ld_call *c = &d->calls[ci];
				uint32_t si = c->input_section, r = c->relocation_index;
				ld_input_section *is = &ctx->inputs.sections.v[si];
				ld_object *o = ctx->inputs.objects.v[is->object];
				if (si != cur) {
					cur = si;
					base = ld_input_section_address(ctx, si, 0);
				}
				const elf_rela *rl = &is->rela[r];
				uint64_t src = base + rl->r_offset;
				{
					uint32_t th = c->thunk;
					if (th != LD_NONE) {
						if (a64_ld_branch_is_in_range(src, a64_ld_thunk_address(ctx, th)))
							continue;
					}
					if (c->destination_section != LD_NONE) {
						/* same value as ld_relocation_target_find(): a plain input section */
						const ld_input_section *ds = &ctx->inputs.sections.v[c->destination_section];
						t.address = ctx->script.sections.v[ds->output_section].address + ds->offset + c->destination_offset;
					} else {
						ld_relocation_target_find(ctx, o, rl->r_info >> 32, rl->r_addend, &t);
						if (t.is_undefined)
							continue;
					}
					if (a64_ld_branch_is_in_range(src, t.address))
						continue;
					/* find a compatible thunk in range */
					uint32_t dg = LD_NONE, dsec = LD_NONE;
					uint64_t dval = 0;
					uint32_t symi = rl->r_info >> 32;
					if (symi >= o->first_global_index) {
						dg = o->global_slots[symi - o->first_global_index];
						ld_global_symbol *gs = &ctx->symbols.globals[dg];
						if (gs->state == LD_SYMBOL_STATE_OBJECT && gs->input_section != LD_NONE) {
							dsec = gs->input_section;
							dval = gs->value;
						}
					} else {
						const elf_symbol *y = &o->symbols[symi];
						dsec = o->first_input_section + y->st_shndx;
						dval = y->st_value;
					}
					uint32_t found = LD_NONE;
					for (uint32_t x = 0; x < ctx->relocations.thunks.n; x++) {
						a64_ld_thunk *tx = &ctx->relocations.thunks.v[x];
						bool same = dsec != LD_NONE ? (tx->input_section == dsec && tx->value == dval)
									 : (tx->global_symbol == dg && tx->input_section == LD_NONE);
						if (same && tx->addend == rl->r_addend &&
						    a64_ld_branch_is_in_range(src, a64_ld_thunk_address(ctx, x))) {
							found = x;
							break;
						}
					}
					if (found == LD_NONE) {
						if (d->thunk_section == LD_NONE) {
							uint32_t ts = ld_input_section_create(ctx, ".text.thunk", LD_SECTION_KIND_THUNK, SHT_PROGBITS,
									       SHF_ALLOC | SHF_EXECINSTR, 4);
							d = &ctx->script.input_descriptions.v[di];
							VVEC_PUSH(ctx->relocations.thunk_sections, ((a64_ld_thunk_section){0}), ld_region_alloc);
							ctx->inputs.sections.v[ts].auxiliary_index = ctx->relocations.thunk_sections.n - 1;
							ctx->inputs.sections.v[ts].output_section = oi;
							ctx->inputs.sections.v[ts].input_description = di;
							ctx->inputs.sections.v[ts].offset = d->thunk_offset;
							d->thunk_section = ts;
						}
						a64_ld_thunk tn = {.global_symbol = dsec == LD_NONE ? dg : LD_NONE, .input_section = dsec, .value = dval,
							    .addend = rl->r_addend, .thunk_section = d->thunk_section};
						if (dg != LD_NONE)
							tn.global_symbol = dsec == LD_NONE ? dg : LD_NONE;
						const char *dn = dg != LD_NONE ? ld_global_name(ctx, dg) : o->string_table + o->symbols[symi].st_name;
						char *nm = ld_region_alloc(strlen(dn) + 20);
						sprintf(nm, "__AArch64ADRPThunk_%s", dn);
						tn.name = nm;
						a64_ld_thunk_section *tsx = &ctx->relocations.thunk_sections.v[ctx->inputs.sections.v[d->thunk_section].auxiliary_index];
						tn.offset = 0;
						VVEC_PUSH(ctx->relocations.thunks, tn, ld_region_alloc);
						VVEC_PUSH(tsx->list, ctx->relocations.thunks.n - 1, ld_region_alloc);
						found = ctx->relocations.thunks.n - 1;
						changed = true;
					}
					a64_ld_thunk_relocation_record(ctx, si, r, found);
					c->thunk = found;
				}
			}
			/* assign thunk offsets in this ld_input_section_description's thunk section */
			if (d->thunk_section != LD_NONE) {
				ld_input_section *ts = &ctx->inputs.sections.v[d->thunk_section];
				a64_ld_thunk_section *tsx = &ctx->relocations.thunk_sections.v[ts->auxiliary_index];
				uint64_t off = 0;
				for (uint32_t x = 0; x < tsx->list.n; x++) {
					a64_ld_thunk *tx = &ctx->relocations.thunks.v[tsx->list.v[x]];
					tx->offset = off;
					/* getMayUseShortThunk(): b if the destination is within
					   +-128 MiB, decided against the current layout */
					if (!tx->is_long) {
						int64_t dd = (int64_t)(a64_ld_thunk_destination(ctx, tsx->list.v[x]) - a64_ld_thunk_address(ctx, tsx->list.v[x]));
						tx->is_long = !ld_value_fits_signed(dd, 28);
					}
					tx->size = tx->is_long ? 12 : 4;
					off += tx->size;
				}
				if (off != ts->size)
					changed = true;
				ts->size = off;
			}
		}
	}
	/* merge newly created thunk sections into their ld_input_section_description lists */
	for (uint32_t i = 0; i < ctx->script.input_descriptions.n; i++) {
		ld_input_section_description *d = &ctx->script.input_descriptions.v[i];
		if (d->thunk_section == LD_NONE)
			continue;
		bool present = false;
		for (uint32_t s = d->sections.n; s-- > 0;)
			if (d->sections.v[s] == d->thunk_section) {
				present = true;
				break;
			}
		if (present)
			continue;
		/* before trailing sections that start at or after the thunk offset */
		uint32_t pos = d->sections.n;
		while (pos > 0 && ctx->inputs.sections.v[d->sections.v[pos - 1]].offset >= d->thunk_offset)
			pos--;
		VVEC_PUSH(d->sections, 0, ld_region_alloc);
		memmove(&d->sections.v[pos + 1], &d->sections.v[pos], (d->sections.n - 1 - pos) * sizeof(uint32_t));
		d->sections.v[pos] = d->thunk_section;
	}
	return changed;
}
