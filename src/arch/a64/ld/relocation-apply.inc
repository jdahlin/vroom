/* arm64 relocation application: values into instruction fields, range checks, .rela.dyn; part of ld/ld.c (#included there) */


static void a64_ld_instruction_immediate_store(uint8_t *loc, uint32_t mask, uint32_t v)
{
	ld_u32_store(loc, (ld_u32_load(loc) & ~mask) | v);
}

static int a64_ld_relocation_error_report(struct ld_context *ctx, const ld_object *o, const ld_input_section *is, const elf_rela *rl, const char *what, int64_t v)
{
	ld_error_report(ctx, "%s:(%s+0x%llx): relocation %u %s (value 0x%llx)", o->path, is->name,
	      (unsigned long long)rl->r_offset, (unsigned)rl->r_info, what, (unsigned long long)v);
	return 0;
}

static bool ld_value_fits_signed(int64_t v, int bits)
{ return v >= -(INT64_C(1) << (bits - 1)) && v < (INT64_C(1) << (bits - 1)); }
/* checkIntUInt: representable as either signed or unsigned */
static bool ld_value_fits_signed_or_unsigned(int64_t v, int bits) { return ld_value_fits_signed(v, bits) || (uint64_t)v < (UINT64_C(1) << bits); }


static void a64_ld_input_section_relocate(struct ld_context *ctx, uint32_t si, uint8_t *buf)
{
	ld_input_section *is = &ctx->inputs.sections.v[si];
	ld_object *o = ctx->inputs.objects.v[is->object];
	uint64_t secva = ld_input_section_address(ctx, si, 0);
	elf_rela *dyn = ctx->relocations.rela_dyn_buffer ? ctx->relocations.rela_dyn_buffer + is->first_dynamic_relocation : NULL;
	uint32_t nd = 0;
	bool alloc_ = is->flags & SHF_ALLOC;
	ld_relocation_target t, t2;
	/* lld AArch64Relaxer (safeToRelaxAdrpLdr): ADRP+LDR GOT pairs relax only if
	   every GOT_PAGE of the section is followed by its LO12 */
	bool got_relax = false;
	if (ctx->options.relax) {
		uint32_t r = 0;

		for (; r < is->rela_count; r++) {
			uint32_t ty = (uint32_t)is->rela[r].r_info;

			if (ty == R_AARCH64_ADR_GOT_PAGE) {
				if (r + 1 < is->rela_count && (uint32_t)is->rela[r + 1].r_info == R_AARCH64_LD64_GOT_LO12_NC) {
					r++;
					continue;
				}
				break;
			}
			if (ty == R_AARCH64_LD64_GOT_LO12_NC)
				break;
		}
		got_relax = r == is->rela_count;
	}
	for (uint32_t r = 0; r < is->rela_count; r++) {
		const elf_rela *rl = &is->rela[r];
		uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;
		uint8_t *loc = buf + rl->r_offset;
		uint64_t P = secva + rl->r_offset;
		if (rl->r_offset >= is->size + (type == R_AARCH64_NONE)) {
			ld_error_report(ctx, "%s:(%s): relocation offset out of range", o->path, is->name);
			continue;
		}
		ld_relocation_target_final_find(ctx, o, symi, rl->r_addend, &t);
		int64_t v;
		switch (type) {
		case R_AARCH64_NONE:
			break;
		case R_AARCH64_ABS64:
			if (ctx->relocations.pic && alloc_ && !t.is_absolute) {
				dyn[nd].r_offset = P;
				dyn[nd].r_info = R_AARCH64_RELATIVE;
				dyn[nd].r_addend = (int64_t)t.address;
				nd++;
				if (!ctx->options.no_apply_dynamic_relocations)
					ld_u64_store(loc, t.address);
			} else {
				ld_u64_store(loc, t.address);
			}
			break;
		case R_AARCH64_ABS32:
			v = (int64_t)t.address;
			if (!ld_value_fits_signed_or_unsigned(v, 32))
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range", v);
			ld_u32_store(loc, (uint32_t)v);
			break;
		case R_AARCH64_ABS16: {
			v = (int64_t)t.address;
			uint16_t x = (uint16_t)v;
			memcpy(loc, &x, 2);
			break;
		}
		case R_AARCH64_PREL64:
			ld_u64_store(loc, t.address - P);
			break;
		case R_AARCH64_PREL32:
			v = (int64_t)(t.address - P);
			if (!ld_value_fits_signed_or_unsigned(v, 32))
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range", v);
			ld_u32_store(loc, (uint32_t)v);
			break;
		case R_AARCH64_PREL16: {
			v = (int64_t)(t.address - P);
			uint16_t x = (uint16_t)v;
			memcpy(loc, &x, 2);
			break;
		}
		case R_AARCH64_CALL26:
		case R_AARCH64_JUMP26: {
			uint32_t th = is->thunked ? a64_ld_thunk_relocation_find(ctx, si, r) : LD_NONE;
			uint64_t dst = th != LD_NONE ? a64_ld_thunk_address(ctx, th) : t.address;
			if (t.is_undefined && th == LD_NONE)
				dst = P + 4;	/* undefined weak: next instruction */
			v = (int64_t)(dst - P);
			if (v & 3)
				a64_ld_relocation_error_report(ctx, o, is, rl, "misaligned branch", v);
			if (!ld_value_fits_signed(v, 28))
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range", v);
			a64_ld_instruction_immediate_store(loc, 0x03ffffff, ((uint64_t)v & 0x0ffffffc) >> 2);
			break;
		}
		case R_AARCH64_ADR_PREL_PG_HI21: {
			if (ctx->options.relax && r + 1 < is->rela_count) {
				const elf_rela *r2 = &is->rela[r + 1];
				if ((uint32_t)r2->r_info == R_AARCH64_ADD_ABS_LO12_NC &&
				    rl->r_offset + 4 == r2->r_offset && (rl->r_info >> 32) == (r2->r_info >> 32) &&
				    rl->r_addend == 0 && r2->r_addend == 0) {
					uint32_t adrp = ld_u32_load(loc), add = ld_u32_load(buf + r2->r_offset);
					uint32_t rd = adrp & 0x1f;
					if ((adrp & 0x9f000000) == 0x90000000 && (add & 0xffc00000) == 0x91000000 &&
					    rd == (add & 0x1f) && rd == ((add >> 5) & 0x1f)) {
						ld_relocation_target_final_find(ctx, o, r2->r_info >> 32, 0, &t2);
						int64_t d = (int64_t)(t2.symbol_address - (secva + r2->r_offset));
						if (d >= -1024 * 1024 && d < 1024 * 1024 && !t2.is_undefined) {
							ld_u32_store(loc, 0xd503201f);
							uint32_t adr = 0x10000000 | rd;
							adr |= ((uint32_t)d & 3) << 29;
							adr |= (((uint32_t)(d >> 2)) & 0x7ffff) << 5;
							ld_u32_store(buf + r2->r_offset, adr);
							r++;
							break;
						}
					}
				}
			}
			v = (int64_t)((t.address & ~0xfffULL) - (P & ~0xfffULL));
			if (!ld_value_fits_signed(v, 33))
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range", v);
			uint64_t imm = (uint64_t)v >> 12;
			a64_ld_instruction_immediate_store(loc, (3u << 29) | (0x7ffffu << 5),
				     (uint32_t)((imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5));
			break;
		}
		case R_AARCH64_ADR_GOT_PAGE:
			/* lld tryRelaxAdrpLdr: adrp+ldr of a non-preemptible symbol's slot ->
			 * adrp+add, then tryRelaxAdrpAdd: nop+adr when within 1 MB.  Under
			 * -shared a global is preemptible unless -Bsymbolic (the kernel's
			 * vmlinux and vDSO links; hidden globals are not relaxed there yet) */
			if (got_relax && (!ctx->options.shared || ctx->options.bsymbolic || !t.is_global) && r + 1 < is->rela_count) {
				const elf_rela *r2 = &is->rela[r + 1];
				uint32_t adrp = ld_u32_load(loc), ldr = rl->r_offset + 4 < is->size ? ld_u32_load(loc + 4) : 0;
				uint32_t rd = adrp & 0x1f;
				if ((uint32_t)r2->r_info == R_AARCH64_LD64_GOT_LO12_NC && rl->r_offset + 4 == r2->r_offset &&
				    (rl->r_info >> 32) == (r2->r_info >> 32) && rl->r_addend == 0 && r2->r_addend == 0 &&
				    !t.is_undefined && !t.is_absolute && (ldr & 0xffc00000) == 0xf9400000 && rd == (ldr & 0x1f) &&
				    rd == ((ldr >> 5) & 0x1f)) {
					int64_t pg = (int64_t)((t.address & ~0xfffULL) - (P & ~0xfffULL));
					int64_t d = (int64_t)(t.address - (P + 4));
					if (ld_value_fits_signed(pg, 33)) {
						if (d >= -1024 * 1024 && d < 1024 * 1024) {
							ld_u32_store(loc, 0xd503201f);
							ld_u32_store(loc + 4, 0x10000000 | rd | ((uint32_t)d & 3) << 29 |
									  (((uint32_t)(d >> 2)) & 0x7ffff) << 5);
						} else {
							uint64_t imm = (uint64_t)pg >> 12;
							ld_u32_store(loc, 0x90000000 | rd | (uint32_t)((imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5));
							ld_u32_store(loc + 4, 0x91000000 | rd | rd << 5 | (uint32_t)(t.address & 0xfff) << 10);
						}
						r++;
						break;
					}
				}
			}
			__attribute__((fallthrough));
		case R_AARCH64_LD64_GOT_LO12_NC: {
			uint64_t slot = ld_input_section_address(ctx, ctx->output.got_section, 8 * (uint64_t)(symi < o->first_global_index ? o->local_got_index[symi] :
									  ctx->symbols.globals[o->global_slots[symi - o->first_global_index]].got_index));
			if (type == R_AARCH64_LD64_GOT_LO12_NC) {
				a64_ld_instruction_immediate_store(loc, 0xfffu << 10, (uint32_t)((slot & 0xfff) >> 3) << 10);
				break;
			}
			v = (int64_t)((slot & ~0xfffULL) - (P & ~0xfffULL));
			if (!ld_value_fits_signed(v, 33))
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range", v);
			uint64_t imm = (uint64_t)v >> 12;
			a64_ld_instruction_immediate_store(loc, (3u << 29) | (0x7ffffu << 5),
				     (uint32_t)((imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5));
			break;
		}
		case R_AARCH64_ADR_PREL_LO21:
			v = (int64_t)(t.address - P);
			if (!ld_value_fits_signed(v, 21))
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range", v);
			a64_ld_instruction_immediate_store(loc, (3u << 29) | (0x7ffffu << 5),
				     (uint32_t)(((uint64_t)v & 3) << 29 | (((uint64_t)v >> 2) & 0x7ffff) << 5));
			break;
		case R_AARCH64_TLSLE_ADD_TPREL_HI12:
			v = (int64_t)(t.address - ctx->layout.tls_thread_pointer);
			if (!ctx->layout.has_tls || v < 0 || v >= 1 << 24)
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range (TLS offset)", v);
			a64_ld_instruction_immediate_store(loc, 0xfffu << 10, (uint32_t)((v >> 12) & 0xfff) << 10);
			break;
		case R_AARCH64_TLSLE_ADD_TPREL_LO12:
		case R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
			v = (int64_t)(t.address - ctx->layout.tls_thread_pointer);
			if (type == R_AARCH64_TLSLE_ADD_TPREL_LO12 && (v < 0 || v >= 1 << 12))
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range (TLS offset)", v);
			a64_ld_instruction_immediate_store(loc, 0xfffu << 10, (uint32_t)(v & 0xfff) << 10);
			break;
		/* lld relaxTlsIeToLe / relaxTlsGdToLe: movz/movk of the offset, nops */
		case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
			v = (int64_t)(t.address - ctx->layout.tls_thread_pointer);
			ld_u32_store(loc, 0xd2a00000u | (ld_u32_load(loc) & 0x1f) | (uint32_t)((v >> 16) & 0xffff) << 5);
			break;
		case R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
			v = (int64_t)(t.address - ctx->layout.tls_thread_pointer);
			ld_u32_store(loc, 0xf2800000u | (ld_u32_load(loc) & 0x1f) | (uint32_t)(v & 0xffff) << 5);
			break;
		case R_AARCH64_TLSDESC_ADR_PAGE21:	/* movz x0, #hi, lsl 16 */
			v = (int64_t)(t.address - ctx->layout.tls_thread_pointer);
			ld_u32_store(loc, 0xd2a00000u | (uint32_t)((v >> 16) & 0xffff) << 5);
			break;
		case R_AARCH64_TLSDESC_LD64_LO12:	/* movk x0, #lo */
			v = (int64_t)(t.address - ctx->layout.tls_thread_pointer);
			ld_u32_store(loc, 0xf2800000u | (uint32_t)(v & 0xffff) << 5);
			break;
		case R_AARCH64_TLSDESC_ADD_LO12:
		case R_AARCH64_TLSDESC_CALL:
			ld_u32_store(loc, 0xd503201fu);
			break;
		case R_AARCH64_ADD_ABS_LO12_NC:
			a64_ld_instruction_immediate_store(loc, 0xfffu << 10, (uint32_t)(t.address & 0xfff) << 10);
			break;
		case R_AARCH64_LDST8_ABS_LO12_NC:
			a64_ld_instruction_immediate_store(loc, 0xfffu << 10, (uint32_t)(t.address & 0xfff) << 10);
			break;
		case R_AARCH64_LDST16_ABS_LO12_NC:
		case R_AARCH64_LDST32_ABS_LO12_NC:
		case R_AARCH64_LDST64_ABS_LO12_NC:
		case R_AARCH64_LDST128_ABS_LO12_NC: {
			int sh = type == R_AARCH64_LDST16_ABS_LO12_NC ? 1 : type == R_AARCH64_LDST32_ABS_LO12_NC ? 2 :
				 type == R_AARCH64_LDST64_ABS_LO12_NC ? 3 : 4;
			if (t.address & ((1u << sh) - 1))
				a64_ld_relocation_error_report(ctx, o, is, rl, "improper alignment", (int64_t)t.address);
			a64_ld_instruction_immediate_store(loc, 0xfffu << 10, (uint32_t)((t.address & 0xfff) >> sh) << 10);
			break;
		}
		case R_AARCH64_LD_PREL_LO19:
			v = (int64_t)(t.address - P);
			if (v & 3)
				a64_ld_relocation_error_report(ctx, o, is, rl, "improper alignment", v);
			if (!ld_value_fits_signed(v, 21))
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range", v);
			a64_ld_instruction_immediate_store(loc, 0x7ffffu << 5, (uint32_t)(((uint64_t)v & 0x1ffffc) << 3));
			break;
		default:
			break;
		}
	}
	/* entries are sorted by offset within the section */
	for (uint32_t a = 1; a < nd; a++) {
		elf_rela x = dyn[a];
		uint32_t b = a;
		while (b > 0 && dyn[b - 1].r_offset > x.r_offset) {
			dyn[b] = dyn[b - 1];
			b--;
		}
		dyn[b] = x;
	}
}
