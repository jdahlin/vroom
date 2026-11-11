/* x86-64 fixups: pc-relative ones, how a value is stored, and whether a
 * fixup resolves in place or becomes a relocation (as LLVM MC's x86 ELF
 * writer).  Every x86 field is a little-endian integer at the fixup. */
#include "../../../obj/objimpl.h"

static int x64_pcrel(uint32_t t)
{
	switch (t) {
	case R_X86_64_PC8: case R_X86_64_PC16: case R_X86_64_PC32: case R_X86_64_PC64: case R_X86_64_PLT32:
	case R_X86_64_GOTPCREL: case R_X86_64_GOTPCRELX: case R_X86_64_REX_GOTPCRELX: case R_X86_64_GOTTPOFF:
	case R_X86_64_TLSGD: case R_X86_64_TLSLD: case R_X86_64_GOTPC32:
		return 1;
	}
	return 0;
}

/* the field's size in bytes */
static int x64_fsize(uint32_t t)
{
	switch (t) {
	case R_X86_64_64: case R_X86_64_PC64: case R_X86_64_DTPOFF64: case R_X86_64_TPOFF64: case R_X86_64_GOTOFF64:
	case R_X86_64_SIZE64:
		return 8;
	case R_X86_64_16: case R_X86_64_PC16:
		return 2;
	case R_X86_64_8: case R_X86_64_PC8:
		return 1;
	default:
		return 4;
	}
}

VPRIV void x64_patch(ElfObj *o, ElfFix *x, int64_t v)
{
	int n = x64_fsize(x->type);

	if (x->type == R_VCC_ABS8)
		n = 1;
	if (n < 8) {
		int64_t lo = -((int64_t)1 << (8 * n - 1)), hi = ((int64_t)1 << (8 * n)) - 1;

		if (x->type == R_X86_64_32S || x64_pcrel(x->type))
			hi = ((int64_t)1 << (8 * n - 1)) - 1;
		if (v < lo || v > hi)
			elf_error(o, "value of %lld is too large for field of %d byte%s", (long long)v, n, n > 1 ? "s" : "");
	}
	put(x->p, (uint64_t)v, n);
}

/* GOT and TLS relocations name their symbol (MC); TLS ones make it STT_TLS */
VPRIV int x64_keepsym(uint32_t t, int *tls)
{
	*tls = t == R_X86_64_TLSGD || t == R_X86_64_TLSLD || t == R_X86_64_GOTTPOFF || t == R_X86_64_TPOFF32 ||
	       t == R_X86_64_TPOFF64 || t == R_X86_64_DTPOFF32 || t == R_X86_64_DTPOFF64;
	return *tls || t == R_X86_64_GOT32 || t == R_X86_64_GOTPCREL || t == R_X86_64_GOTPCRELX || t == R_X86_64_REX_GOTPCRELX ||
	       t == R_X86_64_GOTOFF64 || t == R_X86_64_SIZE32 || t == R_X86_64_SIZE64;
}

VPRIV void x64_resolve(ElfObj *o, Out *w, ElfFix *x)
{
	ElfFrag *f = elf_frag(o, x->frag);
	uint32_t S = f->sec;
	uint64_t P = f->base + x->off;
	ElfSym *a = x->v.add ? elf_symp(o, x->v.add) : 0;
	ElfSym *b = x->v.sub ? elf_symp(o, x->v.sub) : 0;
	int64_t c = x->v.c;
	uint32_t type = x->type;

	if (type == R_VCC_ABS8)
		type = x->type = R_X86_64_8;
	if (a && (a->flags & ESYM_ABS)) { c += (int64_t)a->value; a = 0; }
	if (b && (b->flags & ESYM_ABS)) { c -= (int64_t)b->value; b = 0; }
	if (b) {
		if (!insec(b)) { elf_error(o, "symbol '%s' can not be undefined in a subtraction expression", b->name); return; }
		if (a && insec(a) && symsec(o, a) == symsec(o, b)) {
			x64_patch(o, x, (int64_t)symoff(o, a) - (int64_t)symoff(o, b) + c);
			return;
		}
		if (symsec(o, b) != S) { elf_error(o, "cannot represent a difference across sections"); return; }
		c += (int64_t)P - (int64_t)symoff(o, b);
		switch (type) {
		case R_X86_64_64: type = R_X86_64_PC64; break;
		case R_X86_64_32: case R_X86_64_32S: type = R_X86_64_PC32; break;
		case R_X86_64_16: type = R_X86_64_PC16; break;
		case R_X86_64_8: type = R_X86_64_PC8; break;
		default: elf_error(o, "unsupported pc-relative fixup"); return;
		}
		if (!a) { x64_patch(o, x, c); return; }
		x->type = type;
		elf_reloc(o, w, x, P, type, a, x->v.add, c);
		return;
	}
	if (!a) {
		x64_patch(o, x, x64_pcrel(type) ? c - (int64_t)P : c);
		return;
	}
	if (x64_pcrel(type) && !x->pad && type != R_X86_64_GOTPCREL && type != R_X86_64_GOTPCRELX && type != R_X86_64_REX_GOTPCRELX &&
	    type != R_X86_64_GOTTPOFF && type != R_X86_64_TLSGD && type != R_X86_64_TLSLD && insec(a) && symsec(o, a) == S &&
	    a->bind == STB_LOCAL && a->type != STT_GNU_IFUNC) {
		x64_patch(o, x, (int64_t)symoff(o, a) + c - (int64_t)P);
		return;
	}
	elf_reloc(o, w, x, P, type, a, x->v.add, c);
}
