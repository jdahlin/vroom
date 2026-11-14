/* data-directives.c: the directives that put bytes into a section: .byte/.short/.long/.quad and their
 * aliases, .octa, strings, floats, .base64, alignment, .comm, .space, .fill, .org, .incbin, .reloc,
 * .inst, LEB128.
 *
 * A value that is not a constant becomes a fixup through as_value_emit (resolved or turned into a
 * relocation by the object); .uleb128/.sleb128 of a label difference and .skip with a layout-dependent
 * count become variable items (variable-items.c) instead of bytes.  .incbin reads the file at assembly
 * time, relative to the context's directory (as_include_directory_set) unless the path is absolute.
 * as_data_relocation_type is thread-local state that carries an x86 sym@MOD relocation type from the
 * operand parser to as_value_emit.  Bad operands fail the assembly through as_error_report. */
#include "as-internal.h"
#include <ctype.h>

/* ---------------------------------------------------------------- data */

static _Thread_local uint32_t as_data_relocation_type;	/* x86 data with sym@MOD: the relocation type as_value_emit uses */

VPRIV int as_value_emit(as_context *a, int size, as_value *v)
{
	ElfObj *o = a->object;
	elf_data(o, a->fragment_current);
	if (elf_sec(o, elf_frag(o, a->fragment_current)->sec)->type == SHT_NOBITS) {	/* zeros only (MC) */
		as_value_fold(a, v);
		if (v->add || v->sub || v->c) return as_error_report(a, "cannot have non-zero initializers in a NOBITS section");
		elf_fill(o, a->fragment_current, (uint64_t)size, 0);
		return 0;
	}
	uint64_t off = elf_frag(o, a->fragment_current)->size;
	uint8_t *p = elf_grow(o, a->fragment_current, (size_t)size);
	as_value_fold(a, v);
	if (!v->add && !v->sub) {
		int64_t x = v->c;
		if (size < 8) {
			int64_t lo = -((int64_t)1 << (8 * size - 1)), hi = ((int64_t)1 << (8 * size)) - 1;
			if (x < lo || x > hi) return as_error_report(a, "out of range literal value");
		}
		for (int i = 0; i < size; i++) p[i] = (uint8_t)((uint64_t)x >> 8 * i);
		return 0;
	}
	memset(p, 0, (size_t)size);
	uint32_t t = size == 8 ? R_AARCH64_ABS64 : size == 4 ? R_AARCH64_ABS32 : size == 2 ? R_AARCH64_ABS16 : R_VCC_ABS8;
	if (o->machine == EM_X86_64)
		t = as_data_relocation_type ? as_data_relocation_type : size == 8 ? R_X86_64_64 : size == 4 ? R_X86_64_32 : size == 2 ? R_X86_64_16 : R_X86_64_8;
	elf_fixup(o, a->fragment_current, off, p, t, *v);
	return 0;
}



VPRIV int as_value_emit_relocation(as_context *a, int size, as_value *v, uint32_t type)
{
	as_data_relocation_type = type;
	int r = as_value_emit(a, size, v);
	as_data_relocation_type = 0;
	return r;
}

/* .octa: 16-byte integer literals (only literals: 128-bit expressions do not exist here) */
VPRIV int as_directive_octa(as_context *a, const char *p, const char *e)
{
	for (p = as_whitespace_skip(p, e); p < e;) {
		unsigned __int128 x = 0;
		int neg = 0;
		if (*p == '-') { neg = 1; p++; }
		if (p + 1 < e && p[0] == '0' && (p[1] | 32) == 'x') {
			const char *s = p += 2;
			while (p < e && as_char_is_hex_digit((unsigned char)*p)) { x = x * 16 + (unsigned)(as_char_is_digit((unsigned char)*p) ? *p - '0' : as_char_to_lower((unsigned char)*p) - 'a' + 10); p++; }
			if (p == s) return as_error_report(a, "invalid hexadecimal number");
		} else if (p < e && as_char_is_digit((unsigned char)*p)) {
			while (p < e && as_char_is_digit((unsigned char)*p)) x = x * 10 + (unsigned)(*p++ - '0');
		} else
			return as_error_report(a, "unexpected token in '.octa' directive");
		if (neg) x = -x;
		elf_data(a->object, a->fragment_current);
		uint8_t *q = elf_grow(a->object, a->fragment_current, 16);
		for (int i = 0; i < 16; i++) q[i] = (uint8_t)(x >> 8 * i);
		p = as_whitespace_skip(p, e);
		if (p >= e) break;
		if (*p != ',') return as_error_report(a, "unexpected token in directive");
		p = as_whitespace_skip(p + 1, e);
	}
	return 0;
}

/* .reloc offset, R_AARCH64_<type>[, expr]: a relocation at a constant offset of the current section */
/* a .reloc fixup goes among its frag's fixups in offset order (MC) */
static void as_reloc_fixup_add(ElfObj *o, uint32_t f, uint64_t off, uint8_t *at, uint32_t type, as_value v)
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

VPRIV int as_directive_reloc(as_context *a, const char *p, const char *e)
{
	static const struct { const char *n; uint32_t t; } types[] = {
		{ "R_AARCH64_ABS64", R_AARCH64_ABS64 }, { "R_AARCH64_ABS32", R_AARCH64_ABS32 }, { "R_AARCH64_ABS16", R_AARCH64_ABS16 },
		{ "R_AARCH64_PREL64", R_AARCH64_PREL64 }, { "R_AARCH64_PREL32", R_AARCH64_PREL32 }, { "R_AARCH64_PREL16", R_AARCH64_PREL16 },
		{ "R_AARCH64_NONE", R_AARCH64_NONE }, { "R_AARCH64_TLSDESC_CALL", R_AARCH64_TLSDESC_CALL },
		{ "BFD_RELOC_NONE", R_AARCH64_NONE }, { "BFD_RELOC_64", R_AARCH64_ABS64 },
		{ "BFD_RELOC_32", R_AARCH64_ABS32 }, { "BFD_RELOC_16", R_AARCH64_ABS16 },
	};
	ElfObj *o = a->object;
	as_value ov;
	if (as_expression_value_parse(a, &p, e, &ov)) return -1;
	as_value_fold(a, &ov);
	p = as_whitespace_skip(p, e);
	if (p >= e || *p != ',') return as_error_report(a, "expected comma");
	as_text nm = as_text_first_word(p + 1, e);
	uint32_t type = (uint32_t)-1, size = 0;
	for (size_t i = 0; i < sizeof types / sizeof types[0]; i++)
		if (strlen(types[i].n) == nm.length && !memcmp(types[i].n, nm.text, nm.length)) type = types[i].t;
	if (type == (uint32_t)-1) return as_error_report(a, "unknown relocation name");
	size = type == R_AARCH64_NONE || type == R_AARCH64_TLSDESC_CALL ? 0 :
	       type == R_AARCH64_ABS64 || type == R_AARCH64_PREL64 ? 8 : type == R_AARCH64_ABS32 || type == R_AARCH64_PREL32 ? 4 : 2;
	p = as_whitespace_skip(nm.text + nm.length, e);
	as_value v = { 0, 0, 0 };
	if (p < e && *p == ',') { p++; if (as_expression_value_parse(a, &p, e, &v)) return -1; }
	uint32_t sec = elf_frag(o, a->fragment_current)->sec;
	uint64_t off;
	if (!ov.add && !ov.sub) off = (uint64_t)ov.c;
	else {
		/* .reloc sym+c: an offset in sym's section (MC: the section of the offset) */
		ElfSym *t = ov.add && !ov.sub ? elf_symp(o, ov.add) : 0;
		if (!t || !(t->flags & ESYM_DEFINED) || (t->flags & ESYM_ABS)) return as_error_report(a, ".reloc offset is not absolute or a label");
		if (!size) { as_reloc_fixup_add(o, t->frag, t->value + (uint64_t)ov.c, 0, type, v); return 0; }
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
			if (!size) { as_reloc_fixup_add(o, f, k, 0, type, v); return 0; }
			for (ElfChunk *ch = fr->head; ch; ch = ch->next) {
				if (k + size <= ch->used) {
					/* llvm-mc keeps the bytes and adds the relocation; the field is not patched */
					uint8_t keep[8];
					memcpy(keep, ch->data + k, size);
					as_reloc_fixup_add(o, f, off - at, ch->data + k, type, v);
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
	return as_error_report(a, ".reloc offset is not inside the section's data");
}

VPRIV int as_directive_data(as_context *a, const char *p, const char *e, int size)
{
	if (a->dwarf_row_is_pending) as_dwarf_row_emit(a);
	p = as_whitespace_skip(p, e);
	if (p >= e) return 0;
	for (;;) {
		as_value v;
		if (as_expression_value_parse(a, &p, e, &v)) return -1;
		p = as_whitespace_skip(p, e);
		if (a->object->machine == EM_X86_64 && p < e && *p == '@') {
			/* sym@DTPOFF, @TPOFF, @GOTOFF, @GOTPCREL, @PLT (debug info of TLS, tables) */
			const char *m = ++p;
			while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
			as_text w = { m, (uint32_t)(p - m) };
			as_data_relocation_type = as_text_equals(w, "DTPOFF") ? (size == 8 ? R_X86_64_DTPOFF64 : R_X86_64_DTPOFF32) :
				   as_text_equals(w, "TPOFF") ? (size == 8 ? R_X86_64_TPOFF64 : R_X86_64_TPOFF32) :
				   as_text_equals(w, "GOTOFF") ? R_X86_64_GOTOFF64 : as_text_equals(w, "GOTPCREL") ? R_X86_64_GOTPCREL :
				   as_text_equals(w, "PLT") ? R_X86_64_PLT32 : 0;
			if (a->object->format == OBJ_COFF) {
				/* (MC: only @IMGREL and @SECREL32 change a 4-byte field's relocation; the rest is ignored) */
				int k = as_text_equals(w, "IMGREL") ? 1 : as_text_equals(w, "SECREL32") ? 2 : 0;
				as_data_relocation_type = size == 8 ? R_X86_64_64 : size == 2 ? R_X86_64_16 : size == 1 ? R_X86_64_8 :
							  k == 1 ? R_VCC_COFF_IMGREL32 : k == 2 ? R_VCC_COFF_SECREL32 : R_X86_64_32;
				if (!k && !as_at_is_specifier(m, e)) as_data_relocation_type = 0;
			}
			if (!as_data_relocation_type) return as_error_report(a, "invalid variant '%.*s'", (int)w.length, w.text);
			p = as_whitespace_skip(p, e);
			if (p < e && (*p == '+' || *p == '-')) {
				int64_t k;
				if (as_expression_absolute_parse(a, &p, e, &k)) return -1;
				v.c += k;
			}
		}
		int r = as_value_emit(a, size, &v);
		as_data_relocation_type = 0;
		if (r) return -1;
		p = as_whitespace_skip(p, e);
		if (p >= e) return 0;
		if (*p != ',') return as_error_report(a, "unexpected token in directive");
		p++;
	}
}

VPRIV int as_string_parse(as_context *a, const char **pp, const char *e, char *out, size_t *n)
{
	const char *p = as_whitespace_skip(*pp, e);
	if (p >= e || *p != '"') return as_error_report(a, "expected string");
	p++;
	size_t k = 0;
	while (p < e && *p != '"') {
		char c = *p++;
		if (c == '\\' && p < e) {
			c = *p++;
			switch (c) {
			case 'n': c = '\n'; break; case 't': c = '\t'; break; case 'r': c = '\r'; break;
			case 'b': c = '\b'; break; case 'f': c = '\f'; break; case '"': break; case '\\': break;
			case 'x': { int v = 0; while (p < e && as_char_is_hex_digit((unsigned char)*p)) { v = v * 16 + (as_char_is_digit((unsigned char)*p) ? *p - '0' : as_char_to_lower((unsigned char)*p) - 'a' + 10); p++; } c = (char)v; break; }
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
	if (p >= e) return as_error_report(a, "unterminated string");
	*pp = p + 1;
	*n = k;
	return 0;
}

VPRIV int as_directive_ascii(as_context *a, const char *p, const char *e, int z)
{
	if (a->dwarf_row_is_pending) as_dwarf_row_emit(a);
	p = as_whitespace_skip(p, e);
	if (p >= e) return 0;
	for (;;) {
		char sbuf[4096], *buf = sbuf;	/* the decoded string is no longer than its source */
		size_t n;
		if ((size_t)(e - p) > sizeof sbuf && !(buf = malloc((size_t)(e - p)))) return as_error_report(a, "out of memory");
		if (as_string_parse(a, &p, e, buf, &n)) { if (buf != sbuf) free(buf); return -1; }
		elf_data(a->object, a->fragment_current);
		elf_bytes(a->object, a->fragment_current, buf, n);
		if (buf != sbuf) free(buf);
		if (z) elf_fill(a->object, a->fragment_current, 1, 0);
		p = as_whitespace_skip(p, e);
		if (p >= e) return 0;
		if (*p == ',') p++;
		else if (*p != '"') return as_error_report(a, "unexpected token in directive");
	}
}

/* .float/.single (size 4), .double (8): literals, as llvm-mc (inf, nan, signs) */
VPRIV int as_directive_float(as_context *a, const char *p, const char *e, int size)
{
	if (a->dwarf_row_is_pending) as_dwarf_row_emit(a);
	p = as_whitespace_skip(p, e);
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
			if (end == s || *end) return as_error_report(a, "invalid floating point literal");
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
		as_value v = { 0, 0, (int64_t)bits };
		if (as_value_emit(a, size, &v)) return -1;
		p = as_whitespace_skip(q, e);
		if (p >= e) return 0;
		if (*p != ',') return as_error_report(a, "unexpected token in directive");
		p = as_whitespace_skip(p + 1, e);
	}
}

/* .base64 "..." */
VPRIV int as_directive_base64(as_context *a, const char *p, const char *e)
{
	char buf[4096];
	size_t n;
	if ((size_t)(e - p) > sizeof buf) return as_error_report(a, "string too long");
	if (as_string_parse(a, &p, e, buf, &n)) return -1;
	if (!n || n % 4) return as_error_report(a, "failed to base64 decode string data");
	uint8_t out[3072];
	size_t m = 0;
	for (size_t i = 0; i < n; i += 4) {
		uint32_t w = 0;
		int pad = 0;
		for (int j = 0; j < 4; j++) {
			char c = buf[i + j];
			int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 :
				c == '+' ? 62 : c == '/' ? 63 : c == '=' && i + 4 == n && j >= 2 ? (pad++, 0) : -1;
			if (v < 0 || (pad && c != '=')) return as_error_report(a, "failed to base64 decode string data");
			w = w << 6 | (uint32_t)v;
		}
		out[m++] = (uint8_t)(w >> 16);
		if (pad < 2) out[m++] = (uint8_t)(w >> 8);
		if (pad < 1) out[m++] = (uint8_t)w;
	}
	elf_data(a->object, a->fragment_current);
	elf_bytes(a->object, a->fragment_current, out, m);
	return 0;
}

static int as_section_is_code(as_context *a) { return (elf_sec(a->object, elf_frag(a->object, a->fragment_current)->sec)->flags & (a->object->format == OBJ_COFF ? COFF_SCN_CODE : SHF_EXECINSTR)) != 0; }

/* .align/.p2align (p2 = 1) or .balign (p2 = 0); .balignw/.p2alignl..: a
 * fill of fsz bytes (little-endian pattern) */
VPRIV int as_directive_align(as_context *a, const char *p, const char *e, int p2) { return as_directive_align_width(a, p, e, p2, 1); }
VPRIV int as_directive_align_width(as_context *a, const char *p, const char *e, int p2, int fsz)
{
	int64_t n, fill = 0, max = 0;
	int hasfill = 0;
	if (as_expression_absolute_parse(a, &p, e, &n)) return -1;
	p = as_whitespace_skip(p, e);
	if (p < e && *p == ',') {
		p = as_whitespace_skip(p + 1, e);
		if (p < e && *p != ',') { if (as_expression_absolute_parse(a, &p, e, &fill)) return -1; hasfill = 1; }
		p = as_whitespace_skip(p, e);
		if (p < e && *p == ',') { p++; if (as_expression_absolute_parse(a, &p, e, &max)) return -1; }
	}
	unsigned l2;
	if (p2) {
		if (n < 0 || n >= 32) return as_error_report(a, "invalid alignment value");
		l2 = (unsigned)n;
	} else {
		if (n <= 0) n = 1;
		if (n & (n - 1)) return as_error_report(a, "alignment must be a power of 2");
		l2 = (unsigned)__builtin_ctzll((uint64_t)n);
	}
	if (fsz == 1) {
		if (elf_frag(a->object, a->fragment_current)->split && elf_sec(a->object, elf_frag(a->object, a->fragment_current)->sec)->type != SHT_NOBITS) {
			/* its frag's start is not known yet (after .prefalign/.uleb128): a variable item */
			ElfSec *sc = elf_sec(a->object, elf_frag(a->object, a->fragment_current)->sec);
			if (l2 > sc->align) sc->align = l2;
			int code = !hasfill && as_section_is_code(a);
			struct as_variable_item *v = as_variable_item_add(a, AS_VARIABLE_ITEM_ALIGN);
			v->log2_alignment = (uint8_t)l2; v->code = (uint8_t)code; v->fill = (uint8_t)fill; v->max = (uint64_t)max;
			return 0;
		}
		elf_align(a->object, a->fragment_current, l2, !hasfill && as_section_is_code(a), (uint8_t)fill, (uint64_t)max);
		return 0;
	}
	ElfFrag *f = elf_frag(a->object, a->fragment_current);
	uint64_t al = (uint64_t)1 << l2, pad = (al - (f->size & (al - 1))) & (al - 1);
	ElfSec *sc = elf_sec(a->object, f->sec);
	if (!hasfill || sc->type == SHT_NOBITS) {
		elf_align(a->object, a->fragment_current, l2, !hasfill && as_section_is_code(a), 0, (uint64_t)max);
		return 0;
	}
	/* the pattern repeated over the whole pad (GNU as; llvm-mc lays out the
	 * whole pad but writes only whole values when the pad is not a multiple) */
	if (l2 > sc->align) sc->align = l2;
	if (f->sub) f->has_align |= 1u << l2;
	if (pad && (!max || pad <= (uint64_t)max)) {
		uint8_t *q = elf_grow(a->object, a->fragment_current, pad);
		for (uint64_t i = 0; i < pad; i++) q[i] = (uint8_t)((uint64_t)fill >> 8 * (i % (uint64_t)fsz));
	}
	return 0;
}

/* .comm name, size[, align] (SHN_COMMON, or .bss when the name is local) and .lcomm */
VPRIV int as_directive_comm(as_context *a, const char *p, const char *e, int local)
{
	as_text nm;
	int64_t size, al = 0;
	if (as_name_parse(a, &p, e, &nm)) return -1;
	p = as_whitespace_skip(p, e);
	if (p >= e || *p != ',') return as_error_report(a, "expected a comma");
	p++;
	if (as_expression_absolute_parse(a, &p, e, &size)) return -1;
	p = as_whitespace_skip(p, e);
	if (p < e && *p == ',') { p++; if (as_expression_absolute_parse(a, &p, e, &al)) return -1; }
	if (size < 0) return as_error_report(a, "invalid '.comm' or '.lcomm' directive size");
	int coff = a->object->format == OBJ_COFF;
	if (coff && !local) {	/* COFF: .comm's alignment is a power of two's exponent */
		if (al < 0) return as_error_report(a, "invalid '.comm' or '.lcomm' directive alignment, can't be less than zero");
		if (al > 31) return as_error_report(a, "alignment too large");
		al = (int64_t)1 << al;
	}
	if (al < 0 || (al & (al - 1))) return as_error_report(a, "alignment must be a power of 2");
	uint32_t y = as_symbol_intern(a, nm.text, nm.length);
	ElfSym *s = elf_symp(a->object, y);
	if (s->flags & ESYM_DEFINED) return as_error_report(a, "invalid symbol redefinition");
	elf_reg(a->object, y);
	s->type = STT_OBJECT;
	s->size = (uint64_t)size;
	if (local || ((s->flags & ESYM_BINDSET) && s->bind == STB_LOCAL)) {
		uint32_t f = as_section_standard_find(a, ".bss");
		if (al > 1) elf_align(a->object, f, (unsigned)__builtin_ctzll((uint64_t)al), 0, 0, 0);
		elf_define(a->object, y, f, elf_frag(a->object, f)->size);
		elf_data(a->object, f);
		elf_fill(a->object, f, (uint64_t)size, 0);
		return 0;
	}
	s->common = 1;
	s->value = (uint64_t)(al ? al : 1);
	if (coff && al > 1) coff_align_common(a->object, y, (unsigned)__builtin_ctzll((uint64_t)al));	/* -aligncomm (MinGW) */
	return 0;
}


/* .uleb128/.sleb128 expr, ...: constants (label differences resolved by then) */
VPRIV int as_directive_leb128(as_context *a, const char *p, const char *e, int sgn)
{
	if (a->dwarf_row_is_pending) as_dwarf_row_emit(a);
	for (;;) {
		int64_t v;
		as_value x;
		if (as_expression_value_parse(a, &p, e, &x)) return -1;
		as_value_fold(a, &x);
		if (x.add || x.sub) {	/* a label difference not known yet: a variable item */
			if (x.add && !x.sub) return as_error_report(a, "expected absolute expression");
			struct as_variable_item *vi = as_variable_item_add(a, sgn ? AS_VARIABLE_ITEM_SLEB : AS_VARIABLE_ITEM_ULEB);
			vi->value = x;
			goto next;
		}
		v = x.c;
		elf_data(a->object, a->fragment_current);	/* MC: a relaxable LEB gets no mapping symbol */
		uint8_t t[16]; as_byte_span b = { t, 0 };
		if (sgn) as_leb128_signed_append(&b, v); else as_leb128_unsigned_append(&b, (uint64_t)v);
		memcpy(elf_grow(a->object, a->fragment_current, b.n), t, b.n);
	next:
		p = as_whitespace_skip(p, e);
		if (p >= e) return 0;
		if (*p != ',') return as_error_report(a, "unexpected token in directive");
		p++;
	}
}

VPRIV int as_directive_space(as_context *a, const char *p, const char *e)
{
	int64_t n, fill = 0;
	{
		/* a count only the layout knows (the kernel's ALTERNATIVE: -((b-a)-(d-c) > 0) * ...) */
		const char *q = p;
		as_value v;
		if (as_expression_value_parse(a, &q, e, &v)) return -1;
		if (!as_value_is_absolute(a, &v)) {
			if (!as_value_is_deferrable(a, &v)) return as_error_report(a, "expected absolute expression");
			q = as_whitespace_skip(q, e);
			if (q < e && *q == ',') { q++; if (as_expression_absolute_parse(a, &q, e, &fill)) return -1; }
			elf_data(a->object, a->fragment_current);
			struct as_variable_item *it = as_variable_item_add(a, AS_VARIABLE_ITEM_SKIP);
			it->value = v;
			it->fill = (uint8_t)fill;
			return 0;
		}
	}
	if (as_expression_absolute_parse(a, &p, e, &n)) return -1;
	p = as_whitespace_skip(p, e);
	if (p < e && *p == ',') { p++; if (as_expression_absolute_parse(a, &p, e, &fill)) return -1; }
	if (n < 0) return as_error_report(a, "invalid number of bytes");
	elf_data(a->object, a->fragment_current);
	elf_fill(a->object, a->fragment_current, (uint64_t)n, (uint8_t)fill);
	return 0;
}

VPRIV int as_directive_fill(as_context *a, const char *p, const char *e)
{
	int64_t n, size = 1, val = 0;
	{
		/* .fill end - ., 1, 0xcc: a count the layout settles (the IDT entry stubs), as .skip */
		const char *q = p;
		as_value v;
		if (as_expression_value_parse(a, &q, e, &v)) return -1;
		if (!as_value_is_absolute(a, &v)) {
			int64_t fsz = 1, fv = 0;
			q = as_whitespace_skip(q, e);
			if (q < e && *q == ',') {
				q++; if (as_expression_absolute_parse(a, &q, e, &fsz)) return -1;
				q = as_whitespace_skip(q, e);
				if (q < e && *q == ',') { q++; if (as_expression_absolute_parse(a, &q, e, &fv)) return -1; }
			}
			if (fsz != 1 || !as_value_is_deferrable(a, &v)) return as_error_report(a, "expected absolute expression");
			elf_data(a->object, a->fragment_current);
			struct as_variable_item *it = as_variable_item_add(a, AS_VARIABLE_ITEM_SKIP);
			it->value = v;
			it->fill = (uint8_t)fv;
			return 0;
		}
	}
	if (as_expression_absolute_parse(a, &p, e, &n)) return -1;
	p = as_whitespace_skip(p, e);
	if (p < e && *p == ',') {
		p++; if (as_expression_absolute_parse(a, &p, e, &size)) return -1;
		p = as_whitespace_skip(p, e);
		if (p < e && *p == ',') { p++; if (as_expression_absolute_parse(a, &p, e, &val)) return -1; }
	}
	if (size > 8) size = 8;
	/* MC: .fill makes no mapping symbol (.space does) */
	for (int64_t i = 0; i < n; i++) {
		uint8_t *q = elf_grow(a->object, a->fragment_current, (size_t)size);
		for (int k = 0; k < size; k++) q[k] = (uint8_t)((uint64_t)val >> 8 * k);
	}
	return 0;
}

VPRIV int as_directive_org(as_context *a, const char *p, const char *e)
{
	as_value v;
	int64_t fill = 0;
	if (as_expression_value_parse(a, &p, e, &v)) return -1;
	p = as_whitespace_skip(p, e);
	if (p < e && *p == ',') { p++; if (as_expression_absolute_parse(a, &p, e, &fill)) return -1; }
	ElfFrag *f = elf_frag(a->object, a->fragment_current);
	int64_t target;
	if (v.add && !v.sub) {
		ElfSym *y = elf_symp(a->object, v.add);
		if (!(y->flags & ESYM_DEFINED) || y->frag != a->fragment_current) return as_error_report(a, "unsupported .org target");
		target = (int64_t)y->value + v.c;
	} else if (!v.add && !v.sub) {
		if (f->sub) return as_error_report(a, "unsupported absolute .org in a subsection");
		target = v.c;
	} else return as_error_report(a, "expected assembly-time absolute expression");
	if (target < (int64_t)f->size) return as_error_report(a, "attempt to move .org backwards");
	elf_fill(a->object, a->fragment_current, (uint64_t)target - f->size, (uint8_t)fill);
	return 0;
}

VPRIV int as_directive_incbin(as_context *a, const char *p, const char *e)
{
	char path[1024]; size_t n;
	if ((size_t)(e - p) >= sizeof path) return as_error_report(a, "path too long");
	if (as_string_parse(a, &p, e, path, &n)) return -1;
	path[n] = 0;
	int64_t skip = 0, count = -1;
	p = as_whitespace_skip(p, e);
	if (p < e && *p == ',') {
		p++; if (as_expression_absolute_parse(a, &p, e, &skip)) return -1;
		p = as_whitespace_skip(p, e);
		if (p < e && *p == ',') { p++; if (as_expression_absolute_parse(a, &p, e, &count)) return -1; }
	}
	int fd;
	if (a->incbin_directory && path[0] != '/') {
		char full[2048];
		snprintf(full, sizeof full, "%s/%s", a->incbin_directory, path);
		fd = open(full, O_RDONLY | O_CLOEXEC);
	} else
		fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return as_error_report(a, "Could not find incbin file '%s'", path);
	struct stat st;
	fstat(fd, &st);
	size_t sz = (size_t)st.st_size;
	if ((uint64_t)skip > sz) skip = (int64_t)sz;
	size_t len = sz - (size_t)skip;
	if (count >= 0 && (size_t)count < len) len = (size_t)count;
	elf_data(a->object, a->fragment_current);
	if (len) {
		uint8_t *dst = elf_grow(a->object, a->fragment_current, len);
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

VPRIV int as_directive_inst(as_context *a, const char *p, const char *e)
{
	for (;;) {
		as_value v;
		if (as_expression_value_parse(a, &p, e, &v)) return -1;
		as_value_fold(a, &v);
		if (!as_value_is_absolute(a, &v)) return as_error_report(a, "expected constant expression in .inst");
		if (a->dwarf_row_is_pending) as_dwarf_row_emit(a);
		elf_insn(a->object, a->fragment_current, (uint32_t)v.c);
		p = as_whitespace_skip(p, e);
		if (p >= e) return 0;
		if (*p != ',') return as_error_report(a, "unexpected token in .inst");
		p++;
	}
}
