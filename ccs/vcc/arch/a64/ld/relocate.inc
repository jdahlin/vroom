/* arm64 relocation application: values into instruction fields, range checks, .rela.dyn; part of ld/ld.c (#included there) */


static void write_le_imm(uint8_t *loc, uint32_t mask, uint32_t v)
{
	wr32(loc, (rd32(loc) & ~mask) | v);
}

static int reloc_error(const Obj *o, const ISec *is, const Rela *rl, const char *what, int64_t v)
{
	error("%s:(%s+0x%llx): relocation %u %s (value 0x%llx)", o->path, is->name,
	      (unsigned long long)rl->r_offset, (unsigned)rl->r_info, what, (unsigned long long)v);
	return 0;
}

static bool fits_int(int64_t v, int bits)
{ return v >= -(INT64_C(1) << (bits - 1)) && v < (INT64_C(1) << (bits - 1)); }
/* checkIntUInt: representable as either signed or unsigned */
static bool fits_intuint(int64_t v, int bits) { return fits_int(v, bits) || (uint64_t)v < (UINT64_C(1) << bits); }

static Rela *reladyn_buf;

static void relocate_isec(uint32_t si, uint8_t *buf)
{
	ISec *is = &isecs.v[si];
	Obj *o = objs.v[is->obj];
	uint64_t secva = isec_va(si, 0);
	Rela *dyn = reladyn_buf ? reladyn_buf + is->dyn0 : NULL;
	uint32_t nd = 0;
	bool alloc_ = is->flags & SHF_ALLOC;
	Target t, t2;
	/* lld AArch64Relaxer (safeToRelaxAdrpLdr): ADRP+LDR GOT pairs relax only if
	   every GOT_PAGE of the section is followed by its LO12 */
	bool got_relax = false;
	if (opt.relax) {
		uint32_t r = 0;

		for (; r < is->nrela; r++) {
			uint32_t ty = (uint32_t)is->rela[r].r_info;

			if (ty == R_AARCH64_ADR_GOT_PAGE) {
				if (r + 1 < is->nrela && (uint32_t)is->rela[r + 1].r_info == R_AARCH64_LD64_GOT_LO12_NC) {
					r++;
					continue;
				}
				break;
			}
			if (ty == R_AARCH64_LD64_GOT_LO12_NC)
				break;
		}
		got_relax = r == is->nrela;
	}
	for (uint32_t r = 0; r < is->nrela; r++) {
		const Rela *rl = &is->rela[r];
		uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;
		uint8_t *loc = buf + rl->r_offset;
		uint64_t P = secva + rl->r_offset;
		if (rl->r_offset >= is->size + (type == R_AARCH64_NONE)) {
			error("%s:(%s): relocation offset out of range", o->path, is->name);
			continue;
		}
		reloc_target_final(o, symi, rl->r_addend, &t);
		int64_t v;
		switch (type) {
		case R_AARCH64_NONE:
			break;
		case R_AARCH64_ABS64:
			if (pic && alloc_ && !t.abs) {
				dyn[nd].r_offset = P;
				dyn[nd].r_info = R_AARCH64_RELATIVE;
				dyn[nd].r_addend = (int64_t)t.va;
				nd++;
				if (!opt.no_apply_dyn)
					wr64(loc, t.va);
			} else {
				wr64(loc, t.va);
			}
			break;
		case R_AARCH64_ABS32:
			v = (int64_t)t.va;
			if (!fits_intuint(v, 32))
				reloc_error(o, is, rl, "out of range", v);
			wr32(loc, (uint32_t)v);
			break;
		case R_AARCH64_ABS16: {
			v = (int64_t)t.va;
			uint16_t x = (uint16_t)v;
			memcpy(loc, &x, 2);
			break;
		}
		case R_AARCH64_PREL64:
			wr64(loc, t.va - P);
			break;
		case R_AARCH64_PREL32:
			v = (int64_t)(t.va - P);
			if (!fits_intuint(v, 32))
				reloc_error(o, is, rl, "out of range", v);
			wr32(loc, (uint32_t)v);
			break;
		case R_AARCH64_PREL16: {
			v = (int64_t)(t.va - P);
			uint16_t x = (uint16_t)v;
			memcpy(loc, &x, 2);
			break;
		}
		case R_AARCH64_CALL26:
		case R_AARCH64_JUMP26: {
			uint32_t th = is->thunked ? tr_get(si, r) : NONE;
			uint64_t dst = th != NONE ? thunk_va(th) : t.va;
			if (t.undef && th == NONE)
				dst = P + 4;	/* undefined weak: next instruction */
			v = (int64_t)(dst - P);
			if (v & 3)
				reloc_error(o, is, rl, "misaligned branch", v);
			if (!fits_int(v, 28))
				reloc_error(o, is, rl, "out of range", v);
			write_le_imm(loc, 0x03ffffff, ((uint64_t)v & 0x0ffffffc) >> 2);
			break;
		}
		case R_AARCH64_ADR_PREL_PG_HI21: {
			if (opt.relax && r + 1 < is->nrela) {
				const Rela *r2 = &is->rela[r + 1];
				if ((uint32_t)r2->r_info == R_AARCH64_ADD_ABS_LO12_NC &&
				    rl->r_offset + 4 == r2->r_offset && (rl->r_info >> 32) == (r2->r_info >> 32) &&
				    rl->r_addend == 0 && r2->r_addend == 0) {
					uint32_t adrp = rd32(loc), add = rd32(buf + r2->r_offset);
					uint32_t rd = adrp & 0x1f;
					if ((adrp & 0x9f000000) == 0x90000000 && (add & 0xffc00000) == 0x91000000 &&
					    rd == (add & 0x1f) && rd == ((add >> 5) & 0x1f)) {
						reloc_target_final(o, r2->r_info >> 32, 0, &t2);
						int64_t d = (int64_t)(t2.s - (secva + r2->r_offset));
						if (d >= -1024 * 1024 && d < 1024 * 1024 && !t2.undef) {
							wr32(loc, 0xd503201f);
							uint32_t adr = 0x10000000 | rd;
							adr |= ((uint32_t)d & 3) << 29;
							adr |= (((uint32_t)(d >> 2)) & 0x7ffff) << 5;
							wr32(buf + r2->r_offset, adr);
							r++;
							break;
						}
					}
				}
			}
			v = (int64_t)((t.va & ~0xfffULL) - (P & ~0xfffULL));
			if (!fits_int(v, 33))
				reloc_error(o, is, rl, "out of range", v);
			uint64_t imm = (uint64_t)v >> 12;
			write_le_imm(loc, (3u << 29) | (0x7ffffu << 5),
				     (uint32_t)((imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5));
			break;
		}
		case R_AARCH64_ADR_GOT_PAGE:
			/* lld tryRelaxAdrpLdr: adrp+ldr of a non-preemptible symbol's slot ->
			 * adrp+add, then tryRelaxAdrpAdd: nop+adr when within 1 MB.  Under
			 * -shared a global is preemptible unless -Bsymbolic (the kernel's
			 * vmlinux and vDSO links; hidden globals are not relaxed there yet) */
			if (got_relax && (!opt.shared || opt.bsymbolic || !t.global) && r + 1 < is->nrela) {
				const Rela *r2 = &is->rela[r + 1];
				uint32_t adrp = rd32(loc), ldr = rl->r_offset + 4 < is->size ? rd32(loc + 4) : 0;
				uint32_t rd = adrp & 0x1f;
				if ((uint32_t)r2->r_info == R_AARCH64_LD64_GOT_LO12_NC && rl->r_offset + 4 == r2->r_offset &&
				    (rl->r_info >> 32) == (r2->r_info >> 32) && rl->r_addend == 0 && r2->r_addend == 0 &&
				    !t.undef && !t.abs && (ldr & 0xffc00000) == 0xf9400000 && rd == (ldr & 0x1f) &&
				    rd == ((ldr >> 5) & 0x1f)) {
					int64_t pg = (int64_t)((t.va & ~0xfffULL) - (P & ~0xfffULL));
					int64_t d = (int64_t)(t.va - (P + 4));
					if (fits_int(pg, 33)) {
						if (d >= -1024 * 1024 && d < 1024 * 1024) {
							wr32(loc, 0xd503201f);
							wr32(loc + 4, 0x10000000 | rd | ((uint32_t)d & 3) << 29 |
									  (((uint32_t)(d >> 2)) & 0x7ffff) << 5);
						} else {
							uint64_t imm = (uint64_t)pg >> 12;
							wr32(loc, 0x90000000 | rd | (uint32_t)((imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5));
							wr32(loc + 4, 0x91000000 | rd | rd << 5 | (uint32_t)(t.va & 0xfff) << 10);
						}
						r++;
						break;
					}
				}
			}
			/* fall through */
		case R_AARCH64_LD64_GOT_LO12_NC: {
			uint64_t slot = isec_va(got_isec, 8 * (uint64_t)(symi < o->firstg ? o->lgotidx[symi] :
									  gsyms[o->gs[symi - o->firstg]].gotidx));
			if (type == R_AARCH64_LD64_GOT_LO12_NC) {
				write_le_imm(loc, 0xfffu << 10, (uint32_t)((slot & 0xfff) >> 3) << 10);
				break;
			}
			v = (int64_t)((slot & ~0xfffULL) - (P & ~0xfffULL));
			if (!fits_int(v, 33))
				reloc_error(o, is, rl, "out of range", v);
			uint64_t imm = (uint64_t)v >> 12;
			write_le_imm(loc, (3u << 29) | (0x7ffffu << 5),
				     (uint32_t)((imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5));
			break;
		}
		case R_AARCH64_ADR_PREL_LO21:
			v = (int64_t)(t.va - P);
			if (!fits_int(v, 21))
				reloc_error(o, is, rl, "out of range", v);
			write_le_imm(loc, (3u << 29) | (0x7ffffu << 5),
				     (uint32_t)(((uint64_t)v & 3) << 29 | (((uint64_t)v >> 2) & 0x7ffff) << 5));
			break;
		case R_AARCH64_TLSLE_ADD_TPREL_HI12:
			v = (int64_t)(t.va - tls_tp);
			if (!has_tls || v < 0 || v >= 1 << 24)
				reloc_error(o, is, rl, "out of range (TLS offset)", v);
			write_le_imm(loc, 0xfffu << 10, (uint32_t)((v >> 12) & 0xfff) << 10);
			break;
		case R_AARCH64_TLSLE_ADD_TPREL_LO12:
		case R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
			v = (int64_t)(t.va - tls_tp);
			if (type == R_AARCH64_TLSLE_ADD_TPREL_LO12 && (v < 0 || v >= 1 << 12))
				reloc_error(o, is, rl, "out of range (TLS offset)", v);
			write_le_imm(loc, 0xfffu << 10, (uint32_t)(v & 0xfff) << 10);
			break;
		/* lld relaxTlsIeToLe / relaxTlsGdToLe: movz/movk of the offset, nops */
		case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
			v = (int64_t)(t.va - tls_tp);
			wr32(loc, 0xd2a00000u | (rd32(loc) & 0x1f) | (uint32_t)((v >> 16) & 0xffff) << 5);
			break;
		case R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
			v = (int64_t)(t.va - tls_tp);
			wr32(loc, 0xf2800000u | (rd32(loc) & 0x1f) | (uint32_t)(v & 0xffff) << 5);
			break;
		case R_AARCH64_TLSDESC_ADR_PAGE21:	/* movz x0, #hi, lsl 16 */
			v = (int64_t)(t.va - tls_tp);
			wr32(loc, 0xd2a00000u | (uint32_t)((v >> 16) & 0xffff) << 5);
			break;
		case R_AARCH64_TLSDESC_LD64_LO12:	/* movk x0, #lo */
			v = (int64_t)(t.va - tls_tp);
			wr32(loc, 0xf2800000u | (uint32_t)(v & 0xffff) << 5);
			break;
		case R_AARCH64_TLSDESC_ADD_LO12:
		case R_AARCH64_TLSDESC_CALL:
			wr32(loc, 0xd503201fu);
			break;
		case R_AARCH64_ADD_ABS_LO12_NC:
			write_le_imm(loc, 0xfffu << 10, (uint32_t)(t.va & 0xfff) << 10);
			break;
		case R_AARCH64_LDST8_ABS_LO12_NC:
			write_le_imm(loc, 0xfffu << 10, (uint32_t)(t.va & 0xfff) << 10);
			break;
		case R_AARCH64_LDST16_ABS_LO12_NC:
		case R_AARCH64_LDST32_ABS_LO12_NC:
		case R_AARCH64_LDST64_ABS_LO12_NC:
		case R_AARCH64_LDST128_ABS_LO12_NC: {
			int sh = type == R_AARCH64_LDST16_ABS_LO12_NC ? 1 : type == R_AARCH64_LDST32_ABS_LO12_NC ? 2 :
				 type == R_AARCH64_LDST64_ABS_LO12_NC ? 3 : 4;
			if (t.va & ((1u << sh) - 1))
				reloc_error(o, is, rl, "improper alignment", (int64_t)t.va);
			write_le_imm(loc, 0xfffu << 10, (uint32_t)((t.va & 0xfff) >> sh) << 10);
			break;
		}
		case R_AARCH64_LD_PREL_LO19:
			v = (int64_t)(t.va - P);
			if (v & 3)
				reloc_error(o, is, rl, "improper alignment", v);
			if (!fits_int(v, 21))
				reloc_error(o, is, rl, "out of range", v);
			write_le_imm(loc, 0x7ffffu << 5, (uint32_t)(((uint64_t)v & 0x1ffffc) << 3));
			break;
		default:
			break;
		}
	}
	/* entries are sorted by offset within the section */
	for (uint32_t a = 1; a < nd; a++) {
		Rela x = dyn[a];
		uint32_t b = a;
		while (b > 0 && dyn[b - 1].r_offset > x.r_offset) {
			dyn[b] = dyn[b - 1];
			b--;
		}
		dyn[b] = x;
	}
}
