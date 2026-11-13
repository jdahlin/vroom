/* call-frame-information.c: the .cfi_* directives and the .eh_frame section built from them, as MC's
 * FrameEmitterImpl writes it (CIEs shared by FDEs with the same signal/simple/RA/B-key, entries padded
 * with DW_CFA_nop).
 *
 * The directives only record instructions and frame descriptions in the context; the section is
 * written once, by as_cfi_frame_emit from as_end, after variable items are relaxed, because FDE
 * ranges are label differences that need the final layout.  Errors (an unfinished .cfi_startproc,
 * CFI across sections, more than 8 distinct CIEs, .debug_frame) fail the assembly.  DWARF register
 * numbers follow the object's machine (arm64 or x86-64). */
#include "as-internal.h"

/* ---------------------------------------------------------------- CFI */

enum { AS_CFI_DEF_CFA, AS_CFI_DEF_CFA_OFFSET, AS_CFI_DEF_CFA_REGISTER, AS_CFI_ADJUST, AS_CFI_OFFSET, AS_CFI_REL_OFFSET,
       AS_CFI_RESTORE, AS_CFI_UNDEFINED, AS_CFI_SAME, AS_CFI_REGISTER, AS_CFI_REMEMBER, AS_CFI_RESTORE_STATE, AS_CFI_ESCAPE,
       AS_CFI_NEGATE_RA };

/* a DWARF register: x0-x30/w0-w30, sp/wsp, fp, lr, v/b/h/s/d/q 0-31 (64+), or a number */
/* x86-64's DWARF register numbers (the SysV psABI's) */
static int as_cfi_register_x64_parse(const char *s, size_t n)
{
	static const char *const nm[17] = { "rax", "rdx", "rcx", "rbx", "rsi", "rdi", "rbp", "rsp",
					    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rip" };
	char b[8];

	if (n >= sizeof b)
		return -1;
	for (size_t i = 0; i < n; i++)
		b[i] = (char)as_char_to_lower((unsigned char)s[i]);
	b[n] = 0;
	for (int i = 0; i < 17; i++)
		if (!strcmp(b, nm[i]))
			return i;
	if (!strncmp(b, "xmm", 3) && as_char_is_digit((unsigned char)b[3]) && atoi(b + 3) < 16)
		return 17 + atoi(b + 3);
	return -1;
}

static int as_cfi_register_parse(as_context *a, const char **pp, const char *e, uint32_t *r)
{
	const char *p = as_whitespace_skip(*pp, e), *s = p;
	if (a->object->machine == EM_X86_64 && p < e && *p == '%') {
		int k;
		s = ++p;
		while (p < e && (as_char_is_identifier((unsigned char)*p) || as_char_is_digit((unsigned char)*p))) p++;
		if ((k = as_cfi_register_x64_parse(s, (size_t)(p - s))) < 0) return as_error_report(a, "invalid register name");
		*pp = p; *r = (uint32_t)k;
		return 0;
	}
	while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
	size_t n = (size_t)(p - s);
	if (!n) return as_error_report(a, "expected register");
	*pp = p;
	if (as_char_is_digit((unsigned char)s[0])) {
		uint32_t v = 0;
		for (size_t i = 0; i < n; i++) { if (!as_char_is_digit((unsigned char)s[i])) return as_error_report(a, "invalid register number"); v = v * 10 + (uint32_t)(s[i] - '0'); }
		*r = v; return 0;
	}
	if ((n == 2 && !strncasecmp(s, "sp", 2)) || (n == 3 && !strncasecmp(s, "wsp", 3))) { *r = 31; return 0; }
	if (n == 2 && !strncasecmp(s, "fp", 2)) { *r = 29; return 0; }
	if (n == 2 && !strncasecmp(s, "lr", 2)) { *r = 30; return 0; }
	char c = (char)(s[0] | 0x20);
	if (n >= 2 && n <= 3 && strchr("xwvbhsdq", c) && as_char_is_digit((unsigned char)s[1]) && (n == 2 || as_char_is_digit((unsigned char)s[2]))) {
		uint32_t v = (uint32_t)(s[1] - '0');
		if (n == 3) v = v * 10 + (uint32_t)(s[2] - '0');
		if ((c == 'x' || c == 'w') ? v > 30 : v > 31) return as_error_report(a, "invalid register name");
		*r = (c == 'x' || c == 'w') ? v : 64 + v;
		return 0;
	}
	return as_error_report(a, "invalid register name");
}

static struct as_cfi_instruction *as_cfi_instruction_push(as_context *a, uint8_t op)
{
	if (a->cfi_instruction_count == a->cfi_instruction_capacity) {
		uint32_t nc = a->cfi_instruction_capacity ? a->cfi_instruction_capacity * 2 : 256;
		struct as_cfi_instruction *t = as_region_alloc(&a->definitions, sizeof *t * nc);
		if (a->cfi_instruction_count) memcpy(t, a->cfi_instructions, sizeof *t * a->cfi_instruction_count);
		a->cfi_instructions = t; a->cfi_instruction_capacity = nc;
	}
	struct as_cfi_instruction *c = &a->cfi_instructions[a->cfi_instruction_count++];
	memset(c, 0, sizeof *c);
	c->operation = op;
	c->label = as_label_temporary_define(a);
	a->frame_descriptions[a->frame_description_count - 1].instruction_count++;
	return c;
}

VPRIV int as_directive_cfi(as_context *a, as_text w, const char *p, const char *e)
{
#define AS_CFI_DIRECTIVE_IS(lit) (w.length == sizeof(lit) - 1 && !memcmp(w.text, lit, w.length))
	p = as_whitespace_skip(p, e);
	if (AS_CFI_DIRECTIVE_IS("cfi_sections")) {
		int eh = 0, dbg = 0;
		while (p < e) {
			const char *s = p;
			while (p < e && *p != ',' && *p != ' ' && *p != '\t') p++;
			as_text t = { s, (uint32_t)(p - s) };
			if (as_text_equals(t, ".eh_frame")) eh = 1;
			else if (as_text_equals(t, ".debug_frame")) dbg = 1;
			p = as_whitespace_skip(p, e);
			if (p < e && *p == ',') p = as_whitespace_skip(p + 1, e);
		}
		if (dbg) return as_error_report(a, ".debug_frame is not supported");
		a->cfi_eh_frame_selected = !eh;
		return 0;
	}
	if (AS_CFI_DIRECTIVE_IS("cfi_startproc")) {
		if (a->in_frame_description) return as_error_report(a, "starting new .cfi frame before finishing the previous one");
		if (a->frame_description_count == a->frame_description_capacity) {
			uint32_t nc = a->frame_description_capacity ? a->frame_description_capacity * 2 : 64;
			struct as_frame_description *t = as_region_alloc(&a->definitions, sizeof *t * nc);
			if (a->frame_description_count) memcpy(t, a->frame_descriptions, sizeof *t * a->frame_description_count);
			a->frame_descriptions = t; a->frame_description_capacity = nc;
		}
		struct as_frame_description *f = &a->frame_descriptions[a->frame_description_count++];
		memset(f, 0, sizeof *f);
		as_text t = as_text_trim(p, e);
		if (t.length) { if (!as_text_equals(t, "simple")) return as_error_report(a, "unexpected token"); f->is_simple = 1; }
		f->start = as_label_temporary_define(a); f->first_instruction = a->cfi_instruction_count; f->return_register = a->object->machine == EM_X86_64 ? 16 : 30;
		a->in_frame_description = 1;
		return 0;
	}
	if (!a->in_frame_description) return as_error_report(a, "this directive must appear between .cfi_startproc and .cfi_endproc directives");
	struct as_frame_description *f = &a->frame_descriptions[a->frame_description_count - 1];
	if (AS_CFI_DIRECTIVE_IS("cfi_endproc")) { f->end = as_label_temporary_define(a); a->in_frame_description = 0; return 0; }
	if (AS_CFI_DIRECTIVE_IS("cfi_signal_frame")) { f->is_signal = 1; return 0; }
	if (AS_CFI_DIRECTIVE_IS("cfi_b_key_frame")) { f->uses_b_key = 1; return 0; }
	if (AS_CFI_DIRECTIVE_IS("cfi_return_column")) return as_cfi_register_parse(a, &p, e, &f->return_register);
	if (AS_CFI_DIRECTIVE_IS("cfi_remember_state")) { as_cfi_instruction_push(a, AS_CFI_REMEMBER); return 0; }
	if (AS_CFI_DIRECTIVE_IS("cfi_restore_state")) { as_cfi_instruction_push(a, AS_CFI_RESTORE_STATE); return 0; }
	if (AS_CFI_DIRECTIVE_IS("cfi_negate_ra_state") || AS_CFI_DIRECTIVE_IS("cfi_window_save")) { as_cfi_instruction_push(a, AS_CFI_NEGATE_RA); return 0; }
	if (AS_CFI_DIRECTIVE_IS("cfi_escape")) {
		uint8_t *b = as_region_alloc(&a->definitions, (size_t)(e - p) + 1);
		uint32_t n = 0;
		while (p < e) {
			int64_t v;
			if (as_expression_absolute_parse(a, &p, e, &v)) return -1;
			b[n++] = (uint8_t)v;
			p = as_whitespace_skip(p, e);
			if (p < e && *p == ',') p = as_whitespace_skip(p + 1, e);
		}
		struct as_cfi_instruction *c = as_cfi_instruction_push(a, AS_CFI_ESCAPE);
		c->escape_bytes = b; c->offset = n;
		return 0;
	}
	int64_t off = 0;
	uint32_t r1 = 0, r2 = 0;
	uint8_t op;
	if (AS_CFI_DIRECTIVE_IS("cfi_def_cfa_offset") || AS_CFI_DIRECTIVE_IS("cfi_adjust_cfa_offset")) {
		if (as_expression_absolute_parse(a, &p, e, &off)) return -1;
		op = AS_CFI_DIRECTIVE_IS("cfi_adjust_cfa_offset") ? AS_CFI_ADJUST : AS_CFI_DEF_CFA_OFFSET;
	} else if (AS_CFI_DIRECTIVE_IS("cfi_def_cfa_register") || AS_CFI_DIRECTIVE_IS("cfi_restore") || AS_CFI_DIRECTIVE_IS("cfi_undefined") || AS_CFI_DIRECTIVE_IS("cfi_same_value")) {
		if (as_cfi_register_parse(a, &p, e, &r1)) return -1;
		op = AS_CFI_DIRECTIVE_IS("cfi_def_cfa_register") ? AS_CFI_DEF_CFA_REGISTER : AS_CFI_DIRECTIVE_IS("cfi_restore") ? AS_CFI_RESTORE :
		     AS_CFI_DIRECTIVE_IS("cfi_undefined") ? AS_CFI_UNDEFINED : AS_CFI_SAME;
	} else if (AS_CFI_DIRECTIVE_IS("cfi_def_cfa") || AS_CFI_DIRECTIVE_IS("cfi_offset") || AS_CFI_DIRECTIVE_IS("cfi_rel_offset") || AS_CFI_DIRECTIVE_IS("cfi_register")) {
		if (as_cfi_register_parse(a, &p, e, &r1)) return -1;
		p = as_whitespace_skip(p, e);
		if (p >= e || *p != ',') return as_error_report(a, "expected comma");
		p++;
		if (AS_CFI_DIRECTIVE_IS("cfi_register")) { if (as_cfi_register_parse(a, &p, e, &r2)) return -1; op = AS_CFI_REGISTER; }
		else {
			if (as_expression_absolute_parse(a, &p, e, &off)) return -1;
			op = AS_CFI_DIRECTIVE_IS("cfi_def_cfa") ? AS_CFI_DEF_CFA : AS_CFI_DIRECTIVE_IS("cfi_offset") ? AS_CFI_OFFSET : AS_CFI_REL_OFFSET;
		}
	} else return as_error_report(a, "unknown directive '.%.*s'", (int)w.length, w.text);
	struct as_cfi_instruction *c = as_cfi_instruction_push(a, op);
	c->register_first = r1; c->register_second = r2; c->offset = off;
	return 0;
#undef CFIS
}



/* .eh_frame as MC's FrameEmitterImpl writes it: per FDE, its CIE first when
 * it is new; entries padded to 4 with DW_CFA_nop */
VPRIV void as_cfi_frame_emit(as_context *a)
{
	ElfObj *o = a->object;
	if (a->in_frame_description) { as_error_report(a, "unfinished frame"); return; }
	if (!a->frame_description_count || a->cfi_eh_frame_selected) return;
	int x86 = o->machine == EM_X86_64;
	uint32_t sec = elf_section(o, ".eh_frame", 9, x86 ? 0x70000001u /* SHT_X86_64_UNWIND */ : SHT_PROGBITS, SHF_ALLOC, 0, 0);
	uint32_t fr = sec;
	if (elf_sec(o, sec)->align < 3) elf_sec(o, sec)->align = 3;
	elf_data(o, fr);
	as_fragment_offsets_cache(o);
	struct { uint32_t key, off; } cies[8]; uint32_t ncie = 0;
	size_t cap = 64;
	for (uint32_t i = 0; i < a->frame_description_count; i++) cap += 64 + 16 * (size_t)a->frame_descriptions[i].instruction_count;
	for (uint32_t i = 0; i < a->cfi_instruction_count; i++) if (a->cfi_instructions[i].operation == AS_CFI_ESCAPE) cap += (size_t)a->cfi_instructions[i].offset;
	as_byte_span b = { as_region_alloc(&a->definitions, cap), 0 };
	size_t *pcbs = as_region_alloc(&a->definitions, sizeof(size_t) * a->frame_description_count);
	/* MC stable-sorts the FDEs by CIE key (signal frame, simple, RA column, B key) */
	uint32_t *ord = as_region_alloc(&a->definitions, sizeof(uint32_t) * a->frame_description_count);
	for (uint32_t i = 0; i < a->frame_description_count; i++) {
		struct as_frame_description *f = &a->frame_descriptions[i];
		uint32_t key = (uint32_t)f->is_signal << 30 | (uint32_t)f->is_simple << 29 | f->return_register << 1 | f->uses_b_key;
		uint32_t k = i;
		while (k > 0) {
			struct as_frame_description *g = &a->frame_descriptions[ord[k - 1]];
			uint32_t gk = (uint32_t)g->is_signal << 30 | (uint32_t)g->is_simple << 29 | g->return_register << 1 | g->uses_b_key;
			if (gk <= key) break;
			ord[k] = ord[k - 1]; k--;
		}
		ord[k] = i;
	}
	for (uint32_t oi = 0; oi < a->frame_description_count; oi++) {
		uint32_t i = ord[oi];
		struct as_frame_description *f = &a->frame_descriptions[i];
		uint32_t key = (uint32_t)f->is_signal << 30 | (uint32_t)f->is_simple << 29 | f->return_register << 1 | f->uses_b_key;
		uint32_t ci;
		for (ci = 0; ci < ncie; ci++) if (cies[ci].key == key) break;
		if (ci == ncie) {
			if (ncie == 8) { as_error_report(a, "too many distinct CIEs"); return; }
			cies[ncie].key = key; cies[ncie].off = (uint32_t)b.n; ncie++;
			size_t st = b.n;
			b.n += 4;
			memset(b.p + b.n, 0, 4); b.n += 4;	/* CIE id */
			b.p[b.n++] = 1;				/* version */
			b.p[b.n++] = 'z'; b.p[b.n++] = 'R';
			if (f->is_signal) b.p[b.n++] = 'S';
			if (f->uses_b_key) b.p[b.n++] = 'B';
			b.p[b.n++] = 0;
			as_leb128_unsigned_append(&b, 1); as_leb128_signed_append(&b, x86 ? -8 : -4);
			b.p[b.n++] = (uint8_t)f->return_register;
			as_leb128_unsigned_append(&b, 1); b.p[b.n++] = 0x1b;	/* augmentation: pcrel sdata4 */
			if (!f->is_simple && x86) {	/* cfa = rsp + 8, the return address at cfa - 8 */
				b.p[b.n++] = 0x0c; as_leb128_unsigned_append(&b, 7); as_leb128_unsigned_append(&b, 8);
				b.p[b.n++] = 0x80 | 16; as_leb128_unsigned_append(&b, 1);
			} else if (!f->is_simple) { b.p[b.n++] = 0x0c; as_leb128_unsigned_append(&b, 31); as_leb128_unsigned_append(&b, 0); }
			while ((b.n - st) & 3) b.p[b.n++] = 0;
			uint32_t len = (uint32_t)(b.n - st - 4);
			memcpy(b.p + st, &len, 4);
		}
		size_t st = b.n;
		b.n += 4;
		uint32_t cptr = (uint32_t)(b.n - cies[ci].off);
		memcpy(b.p + b.n, &cptr, 4); b.n += 4;
		size_t pcb = b.n;
		memset(b.p + b.n, 0, 8); b.n += 8;	/* pc_begin, pc_range: fixups */
		as_leb128_unsigned_append(&b, 0);
		int64_t cfa = 0;
		uint32_t base = f->start;
		for (uint32_t k = f->first_instruction; k < f->first_instruction + f->instruction_count; k++) {
			struct as_cfi_instruction *c = &a->cfi_instructions[k];
			ElfSym *x = elf_symp(o, base), *y = elf_symp(o, c->label);
			uint64_t d;
			if (x->frag == y->frag) d = y->value - x->value;
			else if (elf_frag(o, x->frag)->sec == elf_frag(o, y->frag)->sec)	/* split by .prefalign/alignment */
				d = elf_frag(o, y->frag)->base + y->value - elf_frag(o, x->frag)->base - x->value;
			else { as_error_report(a, "CFI across sections is not supported"); return; }
			if (d) {
				if (d < 0x40) b.p[b.n++] = (uint8_t)(0x40 | d);
				else if (d <= 0xff) { b.p[b.n++] = 2; b.p[b.n++] = (uint8_t)d; }
				else if (d <= 0xffff) { b.p[b.n++] = 3; b.p[b.n++] = (uint8_t)d; b.p[b.n++] = (uint8_t)(d >> 8); }
				else { b.p[b.n++] = 4; uint32_t d4 = (uint32_t)d; memcpy(b.p + b.n, &d4, 4); b.n += 4; }
				base = c->label;
			}
			switch (c->operation) {
			case AS_CFI_DEF_CFA: b.p[b.n++] = 0x0c; as_leb128_unsigned_append(&b, c->register_first); cfa = c->offset; as_leb128_unsigned_append(&b, (uint64_t)cfa); break;
			case AS_CFI_DEF_CFA_OFFSET: case AS_CFI_ADJUST:
				cfa = c->operation == AS_CFI_ADJUST ? cfa + c->offset : c->offset;
				b.p[b.n++] = 0x0e; as_leb128_unsigned_append(&b, (uint64_t)cfa); break;
			case AS_CFI_DEF_CFA_REGISTER: b.p[b.n++] = 0x0d; as_leb128_unsigned_append(&b, c->register_first); break;
			case AS_CFI_OFFSET: case AS_CFI_REL_OFFSET: {
				int64_t v = c->offset;
				if (c->operation == AS_CFI_REL_OFFSET) v -= cfa;
				v /= x86 ? -8 : -4;
				if (v < 0) { b.p[b.n++] = 0x11; as_leb128_unsigned_append(&b, c->register_first); as_leb128_signed_append(&b, v); }
				else if (c->register_first < 64) { b.p[b.n++] = (uint8_t)(0x80 | c->register_first); as_leb128_unsigned_append(&b, (uint64_t)v); }
				else { b.p[b.n++] = 0x05; as_leb128_unsigned_append(&b, c->register_first); as_leb128_unsigned_append(&b, (uint64_t)v); }
				break;
			}
			case AS_CFI_RESTORE:
				if (c->register_first < 64) b.p[b.n++] = (uint8_t)(0xc0 | c->register_first);
				else { b.p[b.n++] = 0x06; as_leb128_unsigned_append(&b, c->register_first); }
				break;
			case AS_CFI_UNDEFINED: b.p[b.n++] = 0x07; as_leb128_unsigned_append(&b, c->register_first); break;
			case AS_CFI_SAME: b.p[b.n++] = 0x08; as_leb128_unsigned_append(&b, c->register_first); break;
			case AS_CFI_REGISTER: b.p[b.n++] = 0x09; as_leb128_unsigned_append(&b, c->register_first); as_leb128_unsigned_append(&b, c->register_second); break;
			case AS_CFI_REMEMBER: b.p[b.n++] = 0x0a; break;
			case AS_CFI_RESTORE_STATE: b.p[b.n++] = 0x0b; break;
			case AS_CFI_NEGATE_RA: b.p[b.n++] = 0x2d; break;
			case AS_CFI_ESCAPE: memcpy(b.p + b.n, c->escape_bytes, (size_t)c->offset); b.n += (size_t)c->offset; break;
			}
		}
		while ((b.n - st) & 3) b.p[b.n++] = 0;
		if (oi == a->frame_description_count - 1) while (b.n & 7) b.p[b.n++] = 0;	/* the section ends 8-aligned (MC) */
		uint32_t len = (uint32_t)(b.n - st - 4);
		memcpy(b.p + st, &len, 4);
		pcbs[i] = pcb;
	}
	uint64_t at = elf_frag(o, fr)->size;
	uint8_t *q = elf_grow(o, fr, b.n);
	memcpy(q, b.p, b.n);
	for (uint32_t oi = 0; oi < a->frame_description_count; oi++) {
		uint32_t i = ord[oi];
		struct as_frame_description *f = &a->frame_descriptions[i];
		elf_fixup(o, fr, at + pcbs[i], q + pcbs[i], x86 ? R_X86_64_PC32 : R_AARCH64_PREL32, (as_value){ f->start, 0, 0 });
		elf_fixup(o, fr, at + pcbs[i] + 4, q + pcbs[i] + 4, x86 ? R_X86_64_32 : R_AARCH64_ABS32, (as_value){ f->end, f->start, 0 });
	}
}
