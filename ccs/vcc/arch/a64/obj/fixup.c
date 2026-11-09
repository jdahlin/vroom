/* arm64 fixups: which are pc-relative, how a value is stored into its field,
 * and whether a fixup resolves in place or becomes a relocation (as LLVM MC) */
#include "../../../obj/objimpl.h"

static int is_pcrel(uint32_t t)
{
	switch (t) {
	case R_AARCH64_PREL64: case R_AARCH64_PREL32: case R_AARCH64_PREL16:
	case R_AARCH64_LD_PREL_LO19: case R_AARCH64_ADR_PREL_LO21:
	case R_AARCH64_TSTBR14: case R_AARCH64_CONDBR19:
	case R_AARCH64_JUMP26: case R_AARCH64_CALL26:
		return 1;
	}
	return 0;
}

static uint32_t get32(uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int fits(int64_t v, int bits) { return v >> (bits - 1) == 0 || v >> (bits - 1) == -1; }

/* Apply a resolved value to the field. */
void elf_patch(ElfObj *o, ElfFix *x, int64_t v)
{
	uint8_t *p = x->p;
	uint32_t w;
	switch (x->type) {
	case R_AARCH64_ABS64: case R_AARCH64_PREL64: put(p, (uint64_t)v, 8); return;
	case R_AARCH64_ABS32: case R_AARCH64_PREL32:
		if (!(v >= INT32_MIN && v <= (int64_t)UINT32_MAX)) elf_error(o, "fixup value out of range");
		put(p, (uint64_t)v, 4); return;
	case R_AARCH64_ABS16: case R_AARCH64_PREL16:
		if (!(v >= INT16_MIN && v <= (int64_t)UINT16_MAX)) elf_error(o, "fixup value out of range");
		put(p, (uint64_t)v, 2); return;
	case R_VCC_ABS8:
		if (!(v >= INT8_MIN && v <= (int64_t)UINT8_MAX)) elf_error(o, "fixup value out of range");
		put(p, (uint64_t)v, 1); return;
	}
	w = get32(p);
	switch (x->type) {
	case R_AARCH64_JUMP26: case R_AARCH64_CALL26:
		if (v & 3 || !fits(v, 28)) elf_error(o, "fixup out of range (branch)");
		w |= (uint32_t)(v >> 2) & 0x3ffffff; break;
	case R_AARCH64_CONDBR19: case R_AARCH64_LD_PREL_LO19:
		if (v & 3 || !fits(v, 21)) elf_error(o, "fixup out of range (19-bit)");
		w |= ((uint32_t)(v >> 2) & 0x7ffff) << 5; break;
	case R_AARCH64_TSTBR14:
		if (v & 3 || !fits(v, 16)) elf_error(o, "fixup out of range (14-bit)");
		w |= ((uint32_t)(v >> 2) & 0x3fff) << 5; break;
	case R_AARCH64_ADR_PREL_LO21:
		if (!fits(v, 21)) elf_error(o, "fixup out of range (adr)");
		w |= ((uint32_t)v & 3) << 29 | ((uint32_t)(v >> 2) & 0x7ffff) << 5; break;
	case R_AARCH64_ADR_PREL_PG_HI21: case R_AARCH64_ADR_PREL_PG_HI21_NC:
		v >>= 12;
		w |= ((uint32_t)v & 3) << 29 | ((uint32_t)(v >> 2) & 0x7ffff) << 5; break;
	case R_AARCH64_ADD_ABS_LO12_NC: case R_AARCH64_LDST8_ABS_LO12_NC:
		w |= ((uint32_t)v & 0xfff) << 10; break;
	case R_AARCH64_LDST16_ABS_LO12_NC: w |= ((uint32_t)v & 0xfff) >> 1 << 10; break;
	case R_AARCH64_LDST32_ABS_LO12_NC: w |= ((uint32_t)v & 0xfff) >> 2 << 10; break;
	case R_AARCH64_LDST64_ABS_LO12_NC: w |= ((uint32_t)v & 0xfff) >> 3 << 10; break;
	case R_AARCH64_LDST128_ABS_LO12_NC: w |= ((uint32_t)v & 0xfff) >> 4 << 10; break;
	case R_AARCH64_MOVW_UABS_G0: case R_AARCH64_MOVW_UABS_G0_NC: w |= ((uint32_t)v & 0xffff) << 5; break;
	case R_AARCH64_MOVW_UABS_G1: case R_AARCH64_MOVW_UABS_G1_NC: w |= ((uint32_t)(v >> 16) & 0xffff) << 5; break;
	case R_AARCH64_MOVW_UABS_G2: case R_AARCH64_MOVW_UABS_G2_NC: w |= ((uint32_t)(v >> 32) & 0xffff) << 5; break;
	case R_AARCH64_MOVW_UABS_G3: w |= ((uint32_t)(v >> 48) & 0xffff) << 5; break;
	case R_AARCH64_MOVW_SABS_G0: case R_AARCH64_MOVW_SABS_G1: case R_AARCH64_MOVW_SABS_G2: {
		/* negative: MOVZ becomes MOVN of the inverted value (as MC's backend does) */
		if (v < 0) { v = ~v; w &= ~(1u << 30); }
		unsigned g = x->type - R_AARCH64_MOVW_SABS_G0;
		w |= ((uint32_t)((uint64_t)v >> 16 * g) & 0xffff) << 5;
		break;
	}
	default: elf_error(o, "cannot resolve fixup type %u", x->type); return;
	}
	put(p, w, 4);
}

VPRIV void elf_resolve(ElfObj *o, Out *w, ElfFix *x)
{
	ElfFrag *f = elf_frag(o, x->frag);
	uint32_t S = f->sec;
	uint64_t P = f->base + x->off;
	ElfSym *a = x->v.add ? elf_symp(o, x->v.add) : 0;
	ElfSym *b = x->v.sub ? elf_symp(o, x->v.sub) : 0;
	int64_t c = x->v.c;
	uint32_t type = x->type;
	if (a && (a->flags & ESYM_ABS)) { c += (int64_t)a->value; a = 0; }
	if (b && (b->flags & ESYM_ABS)) { c -= (int64_t)b->value; b = 0; }
	if (b) {
		if (!insec(b)) { elf_error(o, "symbol '%s' can not be undefined in a subtraction expression", b->name); return; }
		if (a && insec(a) && symsec(o, a) == symsec(o, b)) {
			elf_patch(o, x, (int64_t)symoff(o, a) - (int64_t)symoff(o, b) + c);
			return;
		}
		if (symsec(o, b) != S) { elf_error(o, "cannot represent a difference across sections"); return; }
		c += (int64_t)P - (int64_t)symoff(o, b);
		switch (type) {
		case R_AARCH64_ABS64: type = R_AARCH64_PREL64; break;
		case R_AARCH64_ABS32: type = R_AARCH64_PREL32; break;
		case R_AARCH64_ABS16: type = R_AARCH64_PREL16; break;
		default: elf_error(o, "unsupported pc-relative fixup"); return;
		}
		if (!a) { elf_patch(o, x, c); return; }
		x->type = type;
		elf_reloc(o, w, x, P, type, a, x->v.add, c);
		return;
	}
	if (!a) {	/* a constant (branch targets too: the pc-relative offset, as MC) */
		elf_patch(o, x, c);
		return;
	}
	if (is_pcrel(type) && insec(a) && symsec(o, a) == S && a->bind == STB_LOCAL && a->type != STT_GNU_IFUNC) {
		elf_patch(o, x, (int64_t)symoff(o, a) + c - (int64_t)P);
		return;
	}
	elf_reloc(o, w, x, P, type, a, x->v.add, c);
}
