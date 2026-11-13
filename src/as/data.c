/* data.c: data directives: values, strings, floats, alignment, .comm, .space, .fill, .org, .incbin, .reloc, .inst. */
#include "asimpl.h"
#include <ctype.h>

/* ---------------------------------------------------------------- data */

static _Thread_local uint32_t as_dtype;	/* x86 data with sym@MOD: the relocation type emit_value uses */

VPRIV int emit_value(As *a, int size, Val *v)
{
	ElfObj *o = a->o;
	elf_data(o, a->cur);
	if (elf_sec(o, elf_frag(o, a->cur)->sec)->type == SHT_NOBITS) {	/* zeros only (MC) */
		fold(a, v);
		if (v->add || v->sub || v->c) return err(a, "cannot have non-zero initializers in a NOBITS section");
		elf_fill(o, a->cur, (uint64_t)size, 0);
		return 0;
	}
	uint64_t off = elf_frag(o, a->cur)->size;
	uint8_t *p = elf_grow(o, a->cur, (size_t)size);
	fold(a, v);
	if (!v->add && !v->sub) {
		int64_t x = v->c;
		if (size < 8) {
			int64_t lo = -((int64_t)1 << (8 * size - 1)), hi = ((int64_t)1 << (8 * size)) - 1;
			if (x < lo || x > hi) return err(a, "out of range literal value");
		}
		for (int i = 0; i < size; i++) p[i] = (uint8_t)((uint64_t)x >> 8 * i);
		return 0;
	}
	memset(p, 0, (size_t)size);
	uint32_t t = size == 8 ? R_AARCH64_ABS64 : size == 4 ? R_AARCH64_ABS32 : size == 2 ? R_AARCH64_ABS16 : R_VCC_ABS8;
	if (o->machine == EM_X86_64)
		t = as_dtype ? as_dtype : size == 8 ? R_X86_64_64 : size == 4 ? R_X86_64_32 : size == 2 ? R_X86_64_16 : R_X86_64_8;
	elf_fixup(o, a->cur, off, p, t, *v);
	return 0;
}



/* .octa: 16-byte integer literals (only literals: 128-bit expressions do not exist here) */
VPRIV int d_octa(As *a, const char *p, const char *e)
{
	for (p = ws(p, e); p < e;) {
		unsigned __int128 x = 0;
		int neg = 0;
		if (*p == '-') { neg = 1; p++; }
		if (p + 1 < e && p[0] == '0' && (p[1] | 32) == 'x') {
			const char *s = p += 2;
			while (p < e && c_xdigit((unsigned char)*p)) { x = x * 16 + (unsigned)(c_digit((unsigned char)*p) ? *p - '0' : c_lower((unsigned char)*p) - 'a' + 10); p++; }
			if (p == s) return err(a, "invalid hexadecimal number");
		} else if (p < e && c_digit((unsigned char)*p)) {
			while (p < e && c_digit((unsigned char)*p)) x = x * 10 + (unsigned)(*p++ - '0');
		} else
			return err(a, "unexpected token in '.octa' directive");
		if (neg) x = -x;
		elf_data(a->o, a->cur);
		uint8_t *q = elf_grow(a->o, a->cur, 16);
		for (int i = 0; i < 16; i++) q[i] = (uint8_t)(x >> 8 * i);
		p = ws(p, e);
		if (p >= e) break;
		if (*p != ',') return err(a, "unexpected token in directive");
		p = ws(p + 1, e);
	}
	return 0;
}

/* .reloc offset, R_AARCH64_<type>[, expr]: a relocation at a constant offset of the current section */
/* a .reloc fixup goes among its frag's fixups in offset order (MC) */
static void reloc_fixup(ElfObj *o, uint32_t f, uint64_t off, uint8_t *at, uint32_t type, Val v)
{
	elf_fixup(o, f, off, at, type, v);
	ElfFix *fx = (ElfFix *)o->fixes.base;
	ElfFrag *fr = elf_frag(o, f);
	uint32_t id = o->nfix - 1;
	if (fr->fix_head == id) return;
	uint32_t prev = 0, k = fr->fix_head;
	while (k != id && fx[k].off <= off) { prev = k; k = fx[k].next; }
	if (k == id) return;
	/* unlink the tail, insert before k */
	uint32_t t = k;
	while (fx[t].next != id) t = fx[t].next;
	fx[t].next = 0; fr->fix_tail = t;
	fx[id].next = k;
	if (k == fr->fix_head) fr->fix_head = id; else fx[prev].next = id;
}

VPRIV int d_reloc(As *a, const char *p, const char *e)
{
	static const struct { const char *n; uint32_t t; } types[] = {
		{ "R_AARCH64_ABS64", R_AARCH64_ABS64 }, { "R_AARCH64_ABS32", R_AARCH64_ABS32 }, { "R_AARCH64_ABS16", R_AARCH64_ABS16 },
		{ "R_AARCH64_PREL64", R_AARCH64_PREL64 }, { "R_AARCH64_PREL32", R_AARCH64_PREL32 }, { "R_AARCH64_PREL16", R_AARCH64_PREL16 },
		{ "R_AARCH64_NONE", R_AARCH64_NONE }, { "R_AARCH64_TLSDESC_CALL", R_AARCH64_TLSDESC_CALL },
		{ "BFD_RELOC_NONE", R_AARCH64_NONE }, { "BFD_RELOC_64", R_AARCH64_ABS64 },
		{ "BFD_RELOC_32", R_AARCH64_ABS32 }, { "BFD_RELOC_16", R_AARCH64_ABS16 },
	};
	ElfObj *o = a->o;
	Val ov;
	if (val_expr(a, &p, e, &ov)) return -1;
	fold(a, &ov);
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected comma");
	Sv nm = first_word(p + 1, e);
	uint32_t type = (uint32_t)-1, size = 0;
	for (size_t i = 0; i < sizeof types / sizeof types[0]; i++)
		if (strlen(types[i].n) == nm.n && !memcmp(types[i].n, nm.s, nm.n)) type = types[i].t;
	if (type == (uint32_t)-1) return err(a, "unknown relocation name");
	size = type == R_AARCH64_NONE || type == R_AARCH64_TLSDESC_CALL ? 0 :
	       type == R_AARCH64_ABS64 || type == R_AARCH64_PREL64 ? 8 : type == R_AARCH64_ABS32 || type == R_AARCH64_PREL32 ? 4 : 2;
	p = ws(nm.s + nm.n, e);
	Val v = { 0, 0, 0 };
	if (p < e && *p == ',') { p++; if (val_expr(a, &p, e, &v)) return -1; }
	uint32_t sec = elf_frag(o, a->cur)->sec;
	uint64_t off;
	if (!ov.add && !ov.sub) off = (uint64_t)ov.c;
	else {
		/* .reloc sym+c: an offset in sym's section (MC: the section of the offset) */
		ElfSym *t = ov.add && !ov.sub ? elf_symp(o, ov.add) : 0;
		if (!t || !(t->flags & ESYM_DEFINED) || (t->flags & ESYM_ABS)) return err(a, ".reloc offset is not absolute or a label");
		if (!size) { reloc_fixup(o, t->frag, t->value + (uint64_t)ov.c, 0, type, v); return 0; }
		sec = elf_frag(o, t->frag)->sec;
		off = t->value + (uint64_t)ov.c;
		for (uint32_t f = sec; f != t->frag; f = elf_frag(o, f)->next) off += elf_frag(o, f)->size;
	}
	/* the frag and bytes at section offset off */
	uint64_t at = 0;
	for (uint32_t f = sec; f; f = elf_frag(o, f)->next) {
		ElfFrag *fr = elf_frag(o, f);
		if (off >= at && off + size <= at + fr->size + (size == 0)) {
			uint64_t k = off - at;
			if (!size) { reloc_fixup(o, f, k, 0, type, v); return 0; }
			for (ElfChunk *ch = fr->head; ch; ch = ch->next) {
				if (k + size <= ch->used) {
					/* llvm-mc keeps the bytes and adds the relocation; the field is not patched */
					uint8_t keep[8];
					memcpy(keep, ch->data + k, size);
					reloc_fixup(o, f, off - at, ch->data + k, type, v);
					memcpy(ch->data + k, keep, size);
					return 0;
				}
				if (k < ch->used) break;
				k -= ch->used;
			}
			break;
		}
		at += fr->size;
	}
	return err(a, ".reloc offset is not inside the section's data");
}

VPRIV int d_data(As *a, const char *p, const char *e, int size)
{
	if (a->dwlocp) loc_row(a);
	p = ws(p, e);
	if (p >= e) return 0;
	for (;;) {
		Val v;
		if (val_expr(a, &p, e, &v)) return -1;
		p = ws(p, e);
		if (a->o->machine == EM_X86_64 && p < e && *p == '@') {
			/* sym@DTPOFF, @TPOFF, @GOTOFF, @GOTPCREL, @PLT (debug info of TLS, tables) */
			const char *m = ++p;
			while (p < e && isidc((unsigned char)*p)) p++;
			Sv w = { m, (uint32_t)(p - m) };
			as_dtype = sveq(w, "DTPOFF") ? (size == 8 ? R_X86_64_DTPOFF64 : R_X86_64_DTPOFF32) :
				   sveq(w, "TPOFF") ? (size == 8 ? R_X86_64_TPOFF64 : R_X86_64_TPOFF32) :
				   sveq(w, "GOTOFF") ? R_X86_64_GOTOFF64 : sveq(w, "GOTPCREL") ? R_X86_64_GOTPCREL :
				   sveq(w, "PLT") ? R_X86_64_PLT32 : 0;
			if (!as_dtype) return err(a, "invalid variant '%.*s'", (int)w.n, w.s);
			p = ws(p, e);
			if (p < e && (*p == '+' || *p == '-')) {
				int64_t k;
				if (abs_expr(a, &p, e, &k)) return -1;
				v.c += k;
			}
		}
		int r = emit_value(a, size, &v);
		as_dtype = 0;
		if (r) return -1;
		p = ws(p, e);
		if (p >= e) return 0;
		if (*p != ',') return err(a, "unexpected token in directive");
		p++;
	}
}

VPRIV int parse_string(As *a, const char **pp, const char *e, char *out, size_t *n)
{
	const char *p = ws(*pp, e);
	if (p >= e || *p != '"') return err(a, "expected string");
	p++;
	size_t k = 0;
	while (p < e && *p != '"') {
		char c = *p++;
		if (c == '\\' && p < e) {
			c = *p++;
			switch (c) {
			case 'n': c = '\n'; break; case 't': c = '\t'; break; case 'r': c = '\r'; break;
			case 'b': c = '\b'; break; case 'f': c = '\f'; break; case '"': break; case '\\': break;
			case 'x': { int v = 0; while (p < e && c_xdigit((unsigned char)*p)) { v = v * 16 + (c_digit((unsigned char)*p) ? *p - '0' : c_lower((unsigned char)*p) - 'a' + 10); p++; } c = (char)v; break; }
			default:
				if (c >= '0' && c <= '7') {
					int v = c - '0';
					for (int i = 0; i < 2 && p < e && *p >= '0' && *p <= '7'; i++) v = v * 8 + (*p++ - '0');
					c = (char)v;
				}
			}
		}
		out[k++] = c;
	}
	if (p >= e) return err(a, "unterminated string");
	*pp = p + 1;
	*n = k;
	return 0;
}

VPRIV int d_ascii(As *a, const char *p, const char *e, int z)
{
	if (a->dwlocp) loc_row(a);
	p = ws(p, e);
	if (p >= e) return 0;
	for (;;) {
		char sbuf[4096], *buf = sbuf;	/* the decoded string is no longer than its source */
		size_t n;
		if ((size_t)(e - p) > sizeof sbuf && !(buf = malloc((size_t)(e - p)))) return err(a, "out of memory");
		if (parse_string(a, &p, e, buf, &n)) { if (buf != sbuf) free(buf); return -1; }
		elf_data(a->o, a->cur);
		elf_bytes(a->o, a->cur, buf, n);
		if (buf != sbuf) free(buf);
		if (z) elf_fill(a->o, a->cur, 1, 0);
		p = ws(p, e);
		if (p >= e) return 0;
		if (*p == ',') p++;
		else if (*p != '"') return err(a, "unexpected token in directive");
	}
}

/* .float/.single (size 4), .double (8): literals, as llvm-mc (inf, nan, signs) */
VPRIV int d_float(As *a, const char *p, const char *e, int size)
{
	if (a->dwlocp) loc_row(a);
	p = ws(p, e);
	if (p >= e) return 0;
	for (;;) {
		char buf[128];
		size_t k = 0;
		const char *q = p;
		while (q < e && *q != ',' && *q != ' ' && *q != '\t' && k < sizeof buf - 1) buf[k++] = *q++;
		buf[k] = 0;
		char *end;
		const char *s = buf;
		int neg = 0;
		if (*s == '-' || *s == '+') neg = *s++ == '-';
		double d;
		if (!strcasecmp(s, "inf") || !strcasecmp(s, "infinity")) d = __builtin_inf();
		else if (!strcasecmp(s, "nan")) d = __builtin_nan("");
		else {
			d = strtod(s, &end);
			if (end == s || *end) return err(a, "invalid floating point literal");
		}
		uint64_t bits;
		if (__builtin_isnan(d))	/* llvm-mc: every mantissa bit set */
			bits = size == 4 ? 0x7fffffffu | (uint64_t)neg << 31 : 0x7fffffffffffffffu | (uint64_t)neg << 63;
		else if (size == 4) {	/* rounded once, from the text */
			float f = __builtin_isinf(d) || __builtin_isnan(d) ? (float)d : strtof(s, NULL);
			uint32_t b32;
			if (neg) f = -f;
			memcpy(&b32, &f, 4);
			bits = b32;
		} else {
			if (neg) d = -d;
			memcpy(&bits, &d, 8);
		}
		Val v = { 0, 0, (int64_t)bits };
		if (emit_value(a, size, &v)) return -1;
		p = ws(q, e);
		if (p >= e) return 0;
		if (*p != ',') return err(a, "unexpected token in directive");
		p = ws(p + 1, e);
	}
}

/* .base64 "..." */
VPRIV int d_base64(As *a, const char *p, const char *e)
{
	char buf[4096];
	size_t n;
	if ((size_t)(e - p) > sizeof buf) return err(a, "string too long");
	if (parse_string(a, &p, e, buf, &n)) return -1;
	if (!n || n % 4) return err(a, "failed to base64 decode string data");
	uint8_t out[3072];
	size_t m = 0;
	for (size_t i = 0; i < n; i += 4) {
		uint32_t w = 0;
		int pad = 0;
		for (int j = 0; j < 4; j++) {
			char c = buf[i + j];
			int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 :
				c == '+' ? 62 : c == '/' ? 63 : c == '=' && i + 4 == n && j >= 2 ? (pad++, 0) : -1;
			if (v < 0 || (pad && c != '=')) return err(a, "failed to base64 decode string data");
			w = w << 6 | (uint32_t)v;
		}
		out[m++] = (uint8_t)(w >> 16);
		if (pad < 2) out[m++] = (uint8_t)(w >> 8);
		if (pad < 1) out[m++] = (uint8_t)w;
	}
	elf_data(a->o, a->cur);
	elf_bytes(a->o, a->cur, out, m);
	return 0;
}

static int code_section(As *a) { return (elf_sec(a->o, elf_frag(a->o, a->cur)->sec)->flags & SHF_EXECINSTR) != 0; }

/* .align/.p2align (p2 = 1) or .balign (p2 = 0); .balignw/.p2alignl..: a
 * fill of fsz bytes (little-endian pattern) */
VPRIV int d_align(As *a, const char *p, const char *e, int p2) { return d_align_w(a, p, e, p2, 1); }
VPRIV int d_align_w(As *a, const char *p, const char *e, int p2, int fsz)
{
	int64_t n, fill = 0, max = 0;
	int hasfill = 0;
	if (abs_expr(a, &p, e, &n)) return -1;
	p = ws(p, e);
	if (p < e && *p == ',') {
		p = ws(p + 1, e);
		if (p < e && *p != ',') { if (abs_expr(a, &p, e, &fill)) return -1; hasfill = 1; }
		p = ws(p, e);
		if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &max)) return -1; }
	}
	unsigned l2;
	if (p2) {
		if (n < 0 || n >= 32) return err(a, "invalid alignment value");
		l2 = (unsigned)n;
	} else {
		if (n <= 0) n = 1;
		if (n & (n - 1)) return err(a, "alignment must be a power of 2");
		l2 = (unsigned)__builtin_ctzll((uint64_t)n);
	}
	if (fsz == 1) {
		if (elf_frag(a->o, a->cur)->split && elf_sec(a->o, elf_frag(a->o, a->cur)->sec)->type != SHT_NOBITS) {
			/* its frag's start is not known yet (after .prefalign/.uleb128): a variable item */
			ElfSec *sc = elf_sec(a->o, elf_frag(a->o, a->cur)->sec);
			if (l2 > sc->align) sc->align = l2;
			int code = !hasfill && code_section(a);
			struct VarItem *v = var_item(a, VI_ALIGN);
			v->p2 = (uint8_t)l2; v->code = (uint8_t)code; v->fill = (uint8_t)fill; v->max = (uint64_t)max;
			return 0;
		}
		elf_align(a->o, a->cur, l2, !hasfill && code_section(a), (uint8_t)fill, (uint64_t)max);
		return 0;
	}
	ElfFrag *f = elf_frag(a->o, a->cur);
	uint64_t al = (uint64_t)1 << l2, pad = (al - (f->size & (al - 1))) & (al - 1);
	ElfSec *sc = elf_sec(a->o, f->sec);
	if (!hasfill || sc->type == SHT_NOBITS) {
		elf_align(a->o, a->cur, l2, !hasfill && code_section(a), 0, (uint64_t)max);
		return 0;
	}
	/* the pattern repeated over the whole pad (GNU as; llvm-mc lays out the
	 * whole pad but writes only whole values when the pad is not a multiple) */
	if (l2 > sc->align) sc->align = l2;
	if (f->sub) f->has_align |= 1u << l2;
	if (pad && (!max || pad <= (uint64_t)max)) {
		uint8_t *q = elf_grow(a->o, a->cur, pad);
		for (uint64_t i = 0; i < pad; i++) q[i] = (uint8_t)((uint64_t)fill >> 8 * (i % (uint64_t)fsz));
	}
	return 0;
}

/* .comm name, size[, align] (SHN_COMMON, or .bss when the name is local) and .lcomm */
VPRIV int d_comm(As *a, const char *p, const char *e, int local)
{
	Sv nm;
	int64_t size, al = 0;
	if (parse_name(a, &p, e, &nm)) return -1;
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected a comma");
	p++;
	if (abs_expr(a, &p, e, &size)) return -1;
	p = ws(p, e);
	if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &al)) return -1; }
	if (size < 0) return err(a, "invalid '.comm' or '.lcomm' directive size");
	if (al < 0 || (al & (al - 1))) return err(a, "alignment must be a power of 2");
	uint32_t y = sym(a, nm.s, nm.n);
	ElfSym *s = elf_symp(a->o, y);
	if (s->flags & ESYM_DEFINED) return err(a, "invalid symbol redefinition");
	elf_reg(a->o, y);
	s->type = STT_OBJECT;
	s->size = (uint64_t)size;
	if (local || ((s->flags & ESYM_BINDSET) && s->bind == STB_LOCAL)) {
		uint32_t f = std_section(a, ".bss");
		if (al > 1) elf_align(a->o, f, (unsigned)__builtin_ctzll((uint64_t)al), 0, 0, 0);
		elf_define(a->o, y, f, elf_frag(a->o, f)->size);
		elf_data(a->o, f);
		elf_fill(a->o, f, (uint64_t)size, 0);
		return 0;
	}
	s->common = 1;
	s->value = (uint64_t)(al ? al : 1);
	return 0;
}


/* .uleb128/.sleb128 expr, ...: constants (label differences resolved by then) */
VPRIV int d_leb(As *a, const char *p, const char *e, int sgn)
{
	if (a->dwlocp) loc_row(a);
	for (;;) {
		int64_t v;
		Val x;
		if (val_expr(a, &p, e, &x)) return -1;
		fold(a, &x);
		if (x.add || x.sub) {	/* a label difference not known yet: a variable item */
			if (x.add && !x.sub) return err(a, "expected absolute expression");
			struct VarItem *vi = var_item(a, sgn ? VI_SLEB : VI_ULEB);
			vi->v = x;
			goto next;
		}
		v = x.c;
		elf_data(a->o, a->cur);	/* MC: a relaxable LEB gets no mapping symbol */
		uint8_t t[16]; Buf8 b = { t, 0 };
		if (sgn) sleb(&b, v); else uleb(&b, (uint64_t)v);
		memcpy(elf_grow(a->o, a->cur, b.n), t, b.n);
	next:
		p = ws(p, e);
		if (p >= e) return 0;
		if (*p != ',') return err(a, "unexpected token in directive");
		p++;
	}
}

VPRIV int d_space(As *a, const char *p, const char *e)
{
	int64_t n, fill = 0;
	{
		/* a count only the layout knows (the kernel's ALTERNATIVE: -((b-a)-(d-c) > 0) * ...) */
		const char *q = p;
		Val v;
		if (val_expr(a, &q, e, &v)) return -1;
		if (!is_abs(a, &v)) {
			if (!deferrable(a, &v)) return err(a, "expected absolute expression");
			q = ws(q, e);
			if (q < e && *q == ',') { q++; if (abs_expr(a, &q, e, &fill)) return -1; }
			elf_data(a->o, a->cur);
			struct VarItem *it = var_item(a, VI_SKIP);
			it->v = v;
			it->fill = (uint8_t)fill;
			return 0;
		}
	}
	if (abs_expr(a, &p, e, &n)) return -1;
	p = ws(p, e);
	if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &fill)) return -1; }
	if (n < 0) return err(a, "invalid number of bytes");
	elf_data(a->o, a->cur);
	elf_fill(a->o, a->cur, (uint64_t)n, (uint8_t)fill);
	return 0;
}

VPRIV int d_fill(As *a, const char *p, const char *e)
{
	int64_t n, size = 1, val = 0;
	{
		/* .fill end - ., 1, 0xcc: a count the layout settles (the IDT entry stubs), as .skip */
		const char *q = p;
		Val v;
		if (val_expr(a, &q, e, &v)) return -1;
		if (!is_abs(a, &v)) {
			int64_t fsz = 1, fv = 0;
			q = ws(q, e);
			if (q < e && *q == ',') {
				q++; if (abs_expr(a, &q, e, &fsz)) return -1;
				q = ws(q, e);
				if (q < e && *q == ',') { q++; if (abs_expr(a, &q, e, &fv)) return -1; }
			}
			if (fsz != 1 || !deferrable(a, &v)) return err(a, "expected absolute expression");
			elf_data(a->o, a->cur);
			struct VarItem *it = var_item(a, VI_SKIP);
			it->v = v;
			it->fill = (uint8_t)fv;
			return 0;
		}
	}
	if (abs_expr(a, &p, e, &n)) return -1;
	p = ws(p, e);
	if (p < e && *p == ',') {
		p++; if (abs_expr(a, &p, e, &size)) return -1;
		p = ws(p, e);
		if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &val)) return -1; }
	}
	if (size > 8) size = 8;
	/* MC: .fill makes no mapping symbol (.space does) */
	for (int64_t i = 0; i < n; i++) {
		uint8_t *q = elf_grow(a->o, a->cur, (size_t)size);
		for (int k = 0; k < size; k++) q[k] = (uint8_t)((uint64_t)val >> 8 * k);
	}
	return 0;
}

VPRIV int d_org(As *a, const char *p, const char *e)
{
	Val v;
	int64_t fill = 0;
	if (val_expr(a, &p, e, &v)) return -1;
	p = ws(p, e);
	if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &fill)) return -1; }
	ElfFrag *f = elf_frag(a->o, a->cur);
	int64_t target;
	if (v.add && !v.sub) {
		ElfSym *y = elf_symp(a->o, v.add);
		if (!(y->flags & ESYM_DEFINED) || y->frag != a->cur) return err(a, "unsupported .org target");
		target = (int64_t)y->value + v.c;
	} else if (!v.add && !v.sub) {
		if (f->sub) return err(a, "unsupported absolute .org in a subsection");
		target = v.c;
	} else return err(a, "expected assembly-time absolute expression");
	if (target < (int64_t)f->size) return err(a, "attempt to move .org backwards");
	elf_fill(a->o, a->cur, (uint64_t)target - f->size, (uint8_t)fill);
	return 0;
}

VPRIV int d_incbin(As *a, const char *p, const char *e)
{
	char path[1024]; size_t n;
	if ((size_t)(e - p) >= sizeof path) return err(a, "path too long");
	if (parse_string(a, &p, e, path, &n)) return -1;
	path[n] = 0;
	int64_t skip = 0, count = -1;
	p = ws(p, e);
	if (p < e && *p == ',') {
		p++; if (abs_expr(a, &p, e, &skip)) return -1;
		p = ws(p, e);
		if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &count)) return -1; }
	}
	int fd;
	if (a->dir && path[0] != '/') {
		char full[2048];
		snprintf(full, sizeof full, "%s/%s", a->dir, path);
		fd = open(full, O_RDONLY | O_CLOEXEC);
	} else
		fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return err(a, "Could not find incbin file '%s'", path);
	struct stat st;
	fstat(fd, &st);
	size_t sz = (size_t)st.st_size;
	if ((uint64_t)skip > sz) skip = (int64_t)sz;
	size_t len = sz - (size_t)skip;
	if (count >= 0 && (size_t)count < len) len = (size_t)count;
	elf_data(a->o, a->cur);
	if (len) {
		uint8_t *dst = elf_grow(a->o, a->cur, len);
		size_t got = 0;
		while (got < len) {
			ssize_t k = pread(fd, dst + got, len - got, skip + (off_t)got);
			if (k <= 0) break;
			got += (size_t)k;
		}
	}
	close(fd);
	return 0;
}

VPRIV int d_inst(As *a, const char *p, const char *e)
{
	for (;;) {
		Val v;
		if (val_expr(a, &p, e, &v)) return -1;
		fold(a, &v);
		if (!is_abs(a, &v)) return err(a, "expected constant expression in .inst");
		if (a->dwlocp) loc_row(a);
		elf_insn(a->o, a->cur, (uint32_t)v.c);
		p = ws(p, e);
		if (p >= e) return 0;
		if (*p != ',') return err(a, "unexpected token in .inst");
		p++;
	}
}
