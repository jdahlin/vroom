/* as.c: the statement loop of the assembler (GNU as syntax, LLVM MC's semantics): a stack of frames
 * (the input, macro and .rept expansions), the statement scanner, directive dispatch, the
 * instruction hand-off, and the lifecycle (as_new, as_begin, as_snippet_assemble, as_end).
 * Everything else is in the files beside it, one job each; as_SRC in the Makefile lists them. */
#include "as-internal.h"
#include <ctype.h>
#include <stdbool.h>

VPRIV as_text as_text_duplicate(as_context *a, const char *s, size_t n)
{
	char *p = as_region_alloc(&a->definitions, n + 1);
	memcpy(p, s, n); p[n] = 0;
	return (as_text){ p, (uint32_t)n };
}

VPRIV uint32_t as_line_current(as_context *a)
{
	for (int i = a->frame_count - 1; i >= 0; i--) if (a->frames[i].kind == AS_FRAME_FILE) return a->frames[i].statement_line;
	return a->end_line;
}

VPRIV int as_error_report(as_context *a, const char *fmt, ...)
{
	if (a->err) return -1;
	a->err = 1;
	char *m = a->msg + a->mlen;
	size_t sz = sizeof a->msg - a->mlen;
	/* inside a macro or .rept: its body's line, as llvm-mc's <instantiation> buffer */
	bool inst = a->frame_count && a->frames[a->frame_count - 1].kind != AS_FRAME_FILE;
	int k = snprintf(m, sz, "%s%s:%u: error: ", a->mlen ? "\n" : "", inst ? "<instantiation>" : a->file_name,
			 inst ? a->frames[a->frame_count - 1].statement_line : as_line_current(a));
	va_list ap; va_start(ap, fmt);
	if ((size_t)k < sz) vsnprintf(m + k, sz - k, fmt, ap);
	va_end(ap);
	return -1;
}

/* ---------------------------------------------------------------- frames and statements */

VPRIV int as_frame_push(as_context *a, const char *p, size_t n, int kind, size_t mark)
{
	if (a->frame_count >= AS_FRAMES_MAX) return as_error_report(a, "macros nested too deeply");
	as_frame *f = &a->frames[a->frame_count++];
	f->p = p; f->e = p + n; f->kind = (uint8_t)kind; f->at_line_start = 1;
	f->condition_count = (uint32_t)a->condition_count; f->region_mark = mark; f->line = f->statement_line = 1;
	f->caller_line = a->frame_count > 1 ? a->frames[a->frame_count - 2].statement_line : 0;
	return 0;
}

VPRIV void as_frame_pop(as_context *a)
{
	as_frame *f = &a->frames[--a->frame_count];
	if (f->kind != AS_FRAME_FILE) a->expansion_text.used = f->region_mark;
	else a->end_line = f->line;
}

/* bytes as_statement_next must look at: newline, ';', comments, strings */
static const uint8_t as_statement_stop_table[256] = { ['\n'] = 1, [';'] = 1, ['/'] = 1, ['"'] = 1 };

/* first byte of [p, e) in sstop (or e) */
static inline const char *as_statement_stop_find(const char *p, const char *e)
{
#ifdef __aarch64__
	const uint8x16_t nl = vdupq_n_u8('\n'), sc = vdupq_n_u8(';'), sl = vdupq_n_u8('/'), qt = vdupq_n_u8('"');
	while (e - p >= 16) {
		uint8x16_t v = vld1q_u8((const uint8_t *)p);
		uint8x16_t m = vorrq_u8(vorrq_u8(vceqq_u8(v, nl), vceqq_u8(v, sc)), vorrq_u8(vceqq_u8(v, sl), vceqq_u8(v, qt)));
		uint64_t bits = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(m), 4)), 0);
		if (bits) return p + (__builtin_ctzll(bits) >> 2);
		p += 16;
	}
#endif
	while (p < e && !as_statement_stop_table[(uint8_t)*p]) p++;
	return p;
}

/* x86-64 text: '#' starts a comment too */
static inline const char *as_statement_stop_find_x64(const char *p, const char *e)
{
	while (p < e && !as_statement_stop_table[(uint8_t)*p] && *p != '#') p++;
	return p;
}

/* The general case: copy the statement into a->sb without comments. */
static __attribute__((noinline)) size_t as_statement_next_copy(as_context *a, as_frame *f)
{
	const char *p = f->p, *e = f->e;
	char *o = (char *)a->sb.base;
	size_t n = 0, cap = a->sb.cap - 1;
	for (;;) {
		if (p >= e) break;
		char c = *p;
		if (c == '\n') { p++; f->line++; f->at_line_start = 1; break; }
		if (f->at_line_start) {
			if (c == ' ' || c == '\t' || c == '\r') { p++; if (n < cap) o[n++] = c; continue; }
			f->at_line_start = 0;
			if (c == '#') {		/* cpp line marker (# N "file") / line comment */
				const char *q = p + 1;
				while (q < e && (*q == ' ' || *q == '\t')) q++;
				if (f->kind == AS_FRAME_FILE && q < e && as_char_is_digit((unsigned char)*q)) {
					uint32_t ln = 0;
					while (q < e && as_char_is_digit((unsigned char)*q)) ln = ln * 10 + (uint32_t)(*q++ - '0');
					while (q < e && (*q == ' ' || *q == '\t')) q++;
					if (q < e && *q == '"') {
						const char *r = ++q;
						while (q < e && *q != '"' && *q != '\n') q++;
						a->file_name = as_text_duplicate(a, r, (size_t)(q - r)).s;
					}
					if (ln) f->line = ln - 1;	/* the marker's newline makes it ln */
				}
				while (p < e && *p != '\n') p++;
				continue;
			}
		}
		if (c == '#' && a->o->machine == EM_X86_64) {	/* x86: a comment to the end of the line */
			while (p < e && *p != '\n') p++;
			continue;
		}
		if (!as_statement_stop_table[(uint8_t)c]) {	/* plain run: copy it whole */
			const char *q = a->o->machine == EM_X86_64 ? as_statement_stop_find_x64(p + 1, e) : as_statement_stop_find(p + 1, e);
			size_t k = (size_t)(q - p);
			if (k > cap - n) k = cap - n;
			memcpy(o + n, p, k); n += k; p = q;
			continue;
		}
		if (c == ';') { p++; break; }
		if (c == '/' && p + 1 < e && p[1] == '/') { while (p < e && *p != '\n') p++; continue; }
		if (c == '/' && p + 1 < e && p[1] == '*') {
			p += 2;
			while (p + 1 < e && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') f->line++; p++; }
			p += 2;
			if (n < cap) o[n++] = ' ';
			continue;
		}
		if (c == '"') {
			if (n < cap) o[n++] = c;
			p++;
			while (p < e && *p != '"' && *p != '\n') {
				if (*p == '\\' && p + 1 < e) { if (n < cap) o[n++] = *p; p++; }
				if (n < cap) o[n++] = *p;
				p++;
			}
			if (p < e && *p == '"') { if (n < cap) o[n++] = '"'; p++; }
			continue;
		}
		if (n < cap) o[n++] = c;
		p++;
	}
	f->p = p;
	o[n] = 0;
	return n;
}

/* The next statement of f, without comments: *s points at it, either in
 * the frame text (no comment or string in it, the common case) or in
 * a->sb.  Returns its length; sets *raw to where it started in the frame
 * text.  The text is not NUL-terminated. */
VPRIV __attribute__((always_inline)) size_t as_statement_next(as_context *a, as_frame *f, const char **raw, const char **s)
{
	const char *p = f->p, *e = f->e;
	*raw = p;
	f->statement_line = f->line;
	const char *q = p;
	int bol = f->at_line_start;
	if (a->o->machine == EM_X86_64) goto slow;	/* ('#' comments: the copying scanner) */
	if (bol) {
		while (q < e && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
		if (q < e && *q != '\n') bol = 0;
		if (q < e && *q == '#') goto slow;
	}
	q = as_statement_stop_find(q, e);
	if (q < e) {
		if (*q == '\n') { f->line++; bol = 1; }
		else if (*q != ';') goto slow;
		f->p = q + 1;
	} else f->p = q;
	f->at_line_start = (uint8_t)bol;
	*s = p;
	return (size_t)(q - p);
slow:
	*s = (const char *)a->sb.base;
	return as_statement_next_copy(a, f);
}

/* ---------------------------------------------------------------- statements */

static int as_label_define(as_context *a, const char *s, size_t n)
{
	if (as_char_is_digit((unsigned char)s[0])) {
		uint32_t num = 0;
		for (size_t i = 0; i < n; i++) num = num * 10 + (uint32_t)(s[i] - '0');
		as_numeric_label *nl = as_numeric_label_find(a, num);
		uint32_t y = nl->fwd ? nl->fwd : as_symbol_temporary(a);
		nl->fwd = 0;
		nl->cur = y;
		elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
		return 0;
	}
	uint32_t y = as_symbol_intern(a, s, n);
	ElfSym *ys = elf_symp(a->o, y);
	if (ys->flags & ESYM_DEFINED) return as_error_report(a, "invalid symbol redefinition");
	elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
	return 0;
}

/* name = expr / .set name, expr */
static int as_symbol_assign(as_context *a, as_text n, const char *p, const char *e)
{
	if (n.n == 1 && n.s[0] == '.')	/* . = expr: .org expr */
		return as_directive_org(a, p, e);
	uint32_t y = as_symbol_intern(a, n.s, n.n);
	as_value v;
	if (a->o->machine == EM_X86_64) {
		/* name = %reg: the register by another name (crypto code), not a symbol */
		const char *q = as_whitespace_skip(p, e), *r;
		const char *t = e;
		while (t > q && (t[-1] == ' ' || t[-1] == '\t')) t--;
		const char *ar = NULL;
		if (q < t && as_char_is_identifier_start((unsigned char)*q) && a->register_alias_count) {	/* X_ = X0: an alias of an alias */
			for (r = q; r < t && as_char_is_identifier((unsigned char)*r); r++)
				;
			if (r == t) ar = as_register_alias_find(a, elf_sym_lookup(a->o, q, (uint32_t)(t - q)));
		}
		if (ar || (q < t && *q == '%')) {
			if (!ar)
				for (r = q + 1; r < t && (as_char_is_identifier((unsigned char)*r) || as_char_is_digit((unsigned char)*r)); r++)
					;
			else
				q = ar - 1, r = ar + strlen(ar);
			if (r == (ar ? r : t) && r - q - 1 < 8) {
				if (a->register_alias_count == a->register_alias_capacity) {
					uint32_t nc = a->register_alias_capacity ? a->register_alias_capacity * 2 : 32;
					void *nb = as_region_alloc(&a->definitions, sizeof *a->register_aliases * nc);
					if (a->register_alias_count) memcpy(nb, a->register_aliases, sizeof *a->register_aliases * a->register_alias_count);
					a->register_aliases = nb, a->register_alias_capacity = nc;
				}
				a->register_aliases[a->register_alias_count].sym = y;
				memset(a->register_aliases[a->register_alias_count].reg, 0, 8);
				memcpy(a->register_aliases[a->register_alias_count].reg, q + 1, (size_t)(r - q - 1));
				a->register_alias_count++;
				return 0;
			}
		}
		for (uint32_t i = 0; i < a->register_alias_count; i++)	/* (redefined as a value: no longer an alias) */
			if (a->register_aliases[i].sym == y) a->register_aliases[i].reg[0] = 0;
	}
	if (as_expression_value_parse(a, &p, e, &v)) return -1;
	elf_reg(a->o, y);
	elf_reg(a->o, v.add); elf_reg(a->o, v.sub);
	as_value_fold(a, &v);
	ElfSym *s = elf_symp(a->o, y);
	if ((s->flags & ESYM_DEFINED) && !(s->flags & ESYM_ABS) && !(s->flags & ESYM_NOSYMTAB))
		return as_error_report(a, "redefinition of '%.*s'", (int)n.n, n.s);
	as_variable *vr = a->variable_count ? as_variable_slot_find(a, y, 0) : 0;
	if (vr) vr->v.add = (uint32_t)-1;	/* dead */
	if (!v.add && !v.sub) {
		s->flags &= ~ESYM_DEFINED;
		elf_define_abs(a->o, y, (uint64_t)v.c);
		return 0;
	}
	if (v.add && !v.sub) {
		ElfSym *t = elf_symp(a->o, v.add);
		if ((t->flags & ESYM_DEFINED) && !(t->flags & ESYM_SECTION)) {
			s->flags &= ~(ESYM_DEFINED | ESYM_ABS);
			elf_define(a->o, y, t->frag, t->value + (uint64_t)v.c);
			s->alias = t->alias ? t->alias : v.add;
			return 0;
		}
	}
	/* a variable: references see the expression, aliases resolve at the end */
	s->flags &= ~(ESYM_DEFINED | ESYM_ABS);
	vr = as_variable_slot_find(a, y, 1);
	vr->v = v;
	return 0;
}

static int as_directive_dispatch(as_context *a, as_text w, const char *p, const char *e)
{
	const char *d = w.s + 1;
	size_t n = w.n - 1;
#define AS_DIRECTIVE_IS(x) (n == sizeof(x) - 1 && !memcmp(d, x, n))
	if (a->o->machine == EM_X86_64) {
		if (AS_DIRECTIVE_IS("att_syntax") || AS_DIRECTIVE_IS("code64"))
			return 0;	/* (AT&T, 64-bit: what x86-64 text is anyway) */
		if (AS_DIRECTIVE_IS("intel_syntax"))
			return as_error_report(a, "Intel syntax is not supported");
	}
	switch (d[0]) {
	case 'a':
		if (AS_DIRECTIVE_IS("altmacro")) { a->altmacro_mode = 1; return 0; }
		if (AS_DIRECTIVE_IS("addrsig")) { a->o->has_addrsig = 1; return 0; }
		if (AS_DIRECTIVE_IS("addrsig_sym")) {
			as_text nm;
			if (as_name_parse(a, &p, e, &nm)) return -1;
			if (a->addrsig_symbol_count == a->addrsig_symbol_capacity) {
				uint32_t nc = a->addrsig_symbol_capacity ? a->addrsig_symbol_capacity * 2 : 64;
				uint32_t *t = as_region_alloc(&a->definitions, sizeof *t * nc);
				if (a->addrsig_symbol_count) memcpy(t, a->addrsig_symbols, sizeof *t * a->addrsig_symbol_count);
				a->addrsig_symbols = t; a->addrsig_symbol_capacity = nc;
			}
			a->addrsig_symbols[a->addrsig_symbol_count++] = as_symbol_intern(a, nm.s, nm.n);
			return 0;
		}
		if (AS_DIRECTIVE_IS("p2align") || (AS_DIRECTIVE_IS("align") && a->o->machine != EM_X86_64)) return as_directive_align(a, p, e, 1);
		if (AS_DIRECTIVE_IS("align")) return as_directive_align(a, p, e, 0);	/* (x86: a byte count, as .balign) */
		if (AS_DIRECTIVE_IS("ascii")) return as_directive_ascii(a, p, e, 0);
		if (AS_DIRECTIVE_IS("asciz")) return as_directive_ascii(a, p, e, 1);
		if (AS_DIRECTIVE_IS("arch") || AS_DIRECTIVE_IS("arch_extension")) return 0;
		break;
	case 'b':
		if (AS_DIRECTIVE_IS("byte")) return as_directive_data(a, p, e, 1);
		if (AS_DIRECTIVE_IS("balign")) return as_directive_align(a, p, e, 0);
		if (AS_DIRECTIVE_IS("balignw")) return as_directive_align_width(a, p, e, 0, 2);
		if (AS_DIRECTIVE_IS("balignl")) return as_directive_align_width(a, p, e, 0, 4);
		if (AS_DIRECTIVE_IS("base64")) return as_directive_base64(a, p, e);
		if (AS_DIRECTIVE_IS("bss")) { as_fragment_switch(a, as_section_standard_find(a, ".bss")); return 0; }
		break;
	case 'c':
		if (n > 4 && !memcmp(d, "cfi_", 4)) return as_directive_cfi(a, (as_text){ d, (uint32_t)n }, p, e);
		if (AS_DIRECTIVE_IS("comm")) return as_directive_comm(a, p, e, 0);
		if (AS_DIRECTIVE_IS("cpu")) return 0;
		break;
	case 'd':
		if (AS_DIRECTIVE_IS("data")) { as_fragment_switch(a, as_section_standard_find(a, ".data")); return 0; }
		if (AS_DIRECTIVE_IS("double") || AS_DIRECTIVE_IS("dc.d")) return as_directive_float(a, p, e, 8);
		if (AS_DIRECTIVE_IS("dc.s")) return as_directive_float(a, p, e, 4);
		if (AS_DIRECTIVE_IS("dc.b")) return as_directive_data(a, p, e, 1);
		if (AS_DIRECTIVE_IS("dc") || AS_DIRECTIVE_IS("dc.w")) return as_directive_data(a, p, e, 2);
		if (AS_DIRECTIVE_IS("dc.l")) return as_directive_data(a, p, e, 4);
		if (AS_DIRECTIVE_IS("dc.a")) return as_directive_data(a, p, e, 8);
		break;
	case 'e':
		if (AS_DIRECTIVE_IS("equ")) goto set;
		if (AS_DIRECTIVE_IS("end")) { a->end_seen = 1; return 0; }
		if (AS_DIRECTIVE_IS("error")) {
			as_text t = as_text_trim(p, e);
			return as_error_report(a, "%.*s", (int)t.n, t.s);
		}
		if (AS_DIRECTIVE_IS("exitm")) return as_directive_exitm(a);
		if (AS_DIRECTIVE_IS("extern")) return 0;
		if (AS_DIRECTIVE_IS("endm") || AS_DIRECTIVE_IS("endmacro")) return as_error_report(a, "unexpected '%.*s' in file, no current macro definition", (int)w.n, w.s);
		if (AS_DIRECTIVE_IS("endr")) return as_error_report(a, "unexpected '.endr' directive, no current .rept");
		if (AS_DIRECTIVE_IS("equiv")) {
			as_text nm;
			if (as_name_parse(a, &p, e, &nm)) return -1;
			uint32_t y = elf_sym_lookup(a->o, nm.s, nm.n);
			if (y && (elf_symp(a->o, y)->flags & ESYM_DEFINED)) return as_error_report(a, "redefinition of '%.*s'", (int)nm.n, nm.s);
			p = as_whitespace_skip(p, e);
			if (p < e && *p == ',') p++;
			return as_symbol_assign(a, nm, p, e);
		}
		break;
	case 'f':
		if (AS_DIRECTIVE_IS("fill")) return as_directive_fill(a, p, e);
		if (AS_DIRECTIVE_IS("float")) return as_directive_float(a, p, e, 4);
		if (AS_DIRECTIVE_IS("file")) {	/* .file "name": an STT_FILE symbol; .file N ...: DWARF */
			p = as_whitespace_skip(p, e);
			if (p < e && as_char_is_digit((unsigned char)*p)) return as_directive_file(a, p, e);
			if (p < e && *p == '"') {
				const char *q = ++p;
				while (q < e && *q != '"') q++;
				uint32_t y = elf_sym_anon(a->o, as_text_duplicate(a, p, (size_t)(q - p)).s, (uint32_t)(q - p));
				ElfSym *f = elf_symp(a->o, y);
				f->type = STT_FILE;
				elf_define_abs(a->o, y, 0);
				elf_reg(a->o, y);
			}
			return 0;
		}
		break;
	case 'g':
		if (AS_DIRECTIVE_IS("globl") || AS_DIRECTIVE_IS("global")) return as_directive_symbol_attribute(a, p, e, 'g');

		break;
	case 'h':
		if (AS_DIRECTIVE_IS("hword") || AS_DIRECTIVE_IS("half")) return as_directive_data(a, p, e, 2);
		if (AS_DIRECTIVE_IS("hidden")) return as_directive_symbol_attribute(a, p, e, 'h');
		break;
	case 'i':
		if (AS_DIRECTIVE_IS("inst")) return as_directive_inst(a, p, e);
		if (AS_DIRECTIVE_IS("int")) return as_directive_data(a, p, e, 4);
		if (AS_DIRECTIVE_IS("incbin")) return as_directive_incbin(a, p, e);
		if (AS_DIRECTIVE_IS("irp")) return as_directive_irp(a, p, e, 0);
		if (AS_DIRECTIVE_IS("irpc")) return as_directive_irp(a, p, e, 1);
		if (AS_DIRECTIVE_IS("internal")) return as_directive_symbol_attribute(a, p, e, 'i');
		if (AS_DIRECTIVE_IS("ident")) {
			char buf[4096];
			size_t k;
			if ((size_t)(e - p) > sizeof buf) return as_error_report(a, "string too long");
			if (as_string_parse(a, &p, e, buf, &k)) return -1;
			/* MC's emitIdent: into .comment at once, a NUL before the first */
			uint32_t s = elf_section(a->o, ".comment", 8, SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1, 0);
			elf_data(a->o, s);
			if (!a->ident_count++) elf_fill(a->o, s, 1, 0);
			elf_bytes(a->o, s, buf, k);
			elf_fill(a->o, s, 1, 0);
			return 0;
		}
		break;
	case 'l':
		if (AS_DIRECTIVE_IS("loc")) return as_directive_loc(a, p, e);
		if (AS_DIRECTIVE_IS("loc_mark_labels")) return 0;
		if (AS_DIRECTIVE_IS("lcomm")) return as_directive_comm(a, p, e, 1);
		if (AS_DIRECTIVE_IS("long")) return as_directive_data(a, p, e, 4);
		if (AS_DIRECTIVE_IS("local")) return as_directive_symbol_attribute(a, p, e, 'l');
		if (AS_DIRECTIVE_IS("ltorg") || AS_DIRECTIVE_IS("pool")) return a64_as_pools_ltorg(a);
		break;
	case 'm':
		if (AS_DIRECTIVE_IS("macro")) return as_directive_macro(a, p, e);
		break;
	case 'n':
		if (AS_DIRECTIVE_IS("noaltmacro")) { a->altmacro_mode = 0; return 0; }
		break;
	case 'o':
		if (AS_DIRECTIVE_IS("org")) return as_directive_org(a, p, e);
		if (AS_DIRECTIVE_IS("octa")) return as_directive_octa(a, p, e);
		break;
	case 'p':
		if (AS_DIRECTIVE_IS("pushsection")) return as_directive_section(a, p, e, 1);
		if (AS_DIRECTIVE_IS("popsection")) {
			if (!a->section_stack_count) return as_error_report(a, ".popsection without corresponding .pushsection");
			a->section_stack_count--;
			if (a->cur != a->section_stack[a->section_stack_count].cur) a->dwarf_row_is_pending = 0;
			a->cur = as_fragment_latest(a, a->section_stack[a->section_stack_count].cur); a->prev = as_fragment_latest(a, a->section_stack[a->section_stack_count].prev);
			return 0;
		}
		if (AS_DIRECTIVE_IS("prefalign")) return as_directive_prefalign(a, p, e);
		if (AS_DIRECTIVE_IS("previous")) { a->prev = as_fragment_latest(a, a->prev); uint32_t t = a->cur; if (a->prev != t) a->dwarf_row_is_pending = 0; a->cur = a->prev; a->prev = t; return 0; }
		if (AS_DIRECTIVE_IS("p2align")) return as_directive_align(a, p, e, 1);
		if (AS_DIRECTIVE_IS("p2alignw")) return as_directive_align_width(a, p, e, 1, 2);
		if (AS_DIRECTIVE_IS("p2alignl")) return as_directive_align_width(a, p, e, 1, 4);
		if (AS_DIRECTIVE_IS("protected")) return as_directive_symbol_attribute(a, p, e, 'p');
		if (AS_DIRECTIVE_IS("purgem")) return as_directive_purgem(a, p, e);
		if (AS_DIRECTIVE_IS("print")) {
			char buf[4096];
			size_t k;
			if ((size_t)(e - p) > sizeof buf) return as_error_report(a, "string too long");
			if (as_string_parse(a, &p, e, buf, &k)) return -1;
			printf("%.*s\n", (int)k, buf);	/* llvm-mc: to stdout */
			return 0;
		}
		break;
	case 'q':
		if (AS_DIRECTIVE_IS("quad")) return as_directive_data(a, p, e, 8);
		break;
	case 'r':
		if (AS_DIRECTIVE_IS("rept") || AS_DIRECTIVE_IS("rep")) return as_directive_rept(a, p, e);
		if (AS_DIRECTIVE_IS("reloc")) return as_directive_reloc(a, p, e);
		if (AS_DIRECTIVE_IS("rodata")) { as_fragment_switch(a, as_section_standard_find(a, ".rodata")); return 0; }
		break;
	case 's':
		if (AS_DIRECTIVE_IS("section")) return as_directive_section(a, p, e, 0);
		if (AS_DIRECTIVE_IS("set")) goto set;
		if (AS_DIRECTIVE_IS("size")) return as_directive_size(a, p, e);
		if (AS_DIRECTIVE_IS("symver")) return as_directive_symver(a, p, e);
		if (AS_DIRECTIVE_IS("sleb128")) return as_directive_leb128(a, p, e, 1);
		if (AS_DIRECTIVE_IS("short")) return as_directive_data(a, p, e, 2);
		if (AS_DIRECTIVE_IS("single")) return as_directive_float(a, p, e, 4);
		if (AS_DIRECTIVE_IS("space") || AS_DIRECTIVE_IS("skip")) return as_directive_space(a, p, e);
		if (AS_DIRECTIVE_IS("string")) return as_directive_ascii(a, p, e, 1);
		if (AS_DIRECTIVE_IS("subsection")) {
			int64_t sub = 0;
			if (as_whitespace_skip(p, e) < e && as_expression_absolute_parse(a, &p, e, &sub)) return -1;
			as_fragment_switch(a, elf_subsection(a->o, elf_frag(a->o, a->cur)->sec, (int32_t)sub));
			return 0;
		}
		break;
	case 't':
		if (AS_DIRECTIVE_IS("tlsdesccall")) {	/* R_AARCH64_TLSDESC_CALL on the next instruction */
			as_value v;
			if (as_expression_value_parse(a, &p, e, &v)) return -1;
			elf_fixup(a->o, a->cur, elf_frag(a->o, a->cur)->size, 0, R_AARCH64_TLSDESC_CALL, v);
			if (v.add) elf_symp(a->o, v.add)->type = STT_TLS;
			return 0;
		}
		if (AS_DIRECTIVE_IS("text")) { as_fragment_switch(a, as_section_standard_find(a, ".text")); return 0; }
		if (AS_DIRECTIVE_IS("type")) return as_directive_type(a, p, e);
		break;
	case 'u':
		if (AS_DIRECTIVE_IS("uleb128")) return as_directive_leb128(a, p, e, 0);
		if (AS_DIRECTIVE_IS("unreq")) {
			as_text nm = as_text_first_word(p, e);
			for (uint32_t i = a->alias_count; i-- > 0;)
				if (a->al[i].name.n == nm.n && !strncasecmp(a->al[i].name.s, nm.s, nm.n)) {
					a->al[i] = a->al[--a->alias_count];
					as_alias_rehash(a);
					return 0;
				}
			return 0;
		}
		break;
	case 'v':
		if (AS_DIRECTIVE_IS("variant_pcs")) {	/* STO_AARCH64_VARIANT_PCS */
			as_text nm;
			if (as_name_parse(a, &p, e, &nm)) return -1;
			uint32_t y = as_symbol_intern(a, nm.s, nm.n);
			elf_symp(a->o, y)->vis |= 0x80;
			elf_reg(a->o, y);
			return 0;
		}
		break;
	case 'w':
		if (AS_DIRECTIVE_IS("weakref")) {	/* .weakref alias, target */
			as_text al, tg;
			if (as_name_parse(a, &p, e, &al)) return -1;
			p = as_whitespace_skip(p, e);
			if (p >= e || *p != ',') return as_error_report(a, "expected a comma");
			p++;
			if (as_name_parse(a, &p, e, &tg)) return -1;
			uint32_t ya = as_symbol_intern(a, al.s, al.n), yt = as_symbol_intern(a, tg.s, tg.n);
			if (a->symver_count == a->symver_capacity) {
				uint32_t nc = a->symver_capacity ? a->symver_capacity * 2 : 16;
				void *t = as_region_alloc(&a->definitions, sizeof *a->symvers * nc);
				if (a->symver_count) memcpy(t, a->symvers, sizeof *a->symvers * a->symver_count);
				a->symvers = t; a->symver_capacity = nc;
			}
			a->symvers[a->symver_count].orig = ya; a->symvers[a->symver_count].ver = yt; a->symvers[a->symver_count].rm = 2;
			a->symver_count++;
			return 0;
		}
		if (AS_DIRECTIVE_IS("word")) return as_directive_data(a, p, e, a->o->machine == EM_X86_64 ? 2 : 4);	/* (x86: 2 bytes, as GNU as) */
		if (AS_DIRECTIVE_IS("weak")) return as_directive_symbol_attribute(a, p, e, 'w');
		if (AS_DIRECTIVE_IS("warning")) {
			as_text t = as_text_trim(p, e);
			char buf[4096];
			size_t k = 0;
			const char *q = t.s;
			if (t.n && *q == '"' && (size_t)(e - q) <= sizeof buf && !as_string_parse(a, &q, e, buf, &k))
				fprintf(stderr, "%s: warning: %.*s\n", a->file_name ? a->file_name : "<stdin>", (int)k, buf);
			else
				fprintf(stderr, "%s: warning: .warning directive invoked in source file\n", a->file_name ? a->file_name : "<stdin>");
			return 0;
		}
		break;
	case 'x':
		if (AS_DIRECTIVE_IS("xword")) return as_directive_data(a, p, e, 8);
		break;
	case 'z':
		if (AS_DIRECTIVE_IS("zero")) return as_directive_space(a, p, e);
		break;
	case '2':
		if (AS_DIRECTIVE_IS("2byte")) return as_directive_data(a, p, e, 2);
		break;
	case '4':
		if (AS_DIRECTIVE_IS("4byte")) return as_directive_data(a, p, e, 4);
		break;
	case '8':
		if (AS_DIRECTIVE_IS("8byte")) return as_directive_data(a, p, e, 8);
		break;
	}
	return as_error_report(a, "unknown directive '%.*s'", (int)w.n, w.s);
set:;
	as_text nm;
	if (as_name_parse(a, &p, e, &nm)) return -1;
	p = as_whitespace_skip(p, e);
	if (p >= e || *p != ',') return as_error_report(a, "expected comma");
	return as_symbol_assign(a, nm, p + 1, e);
#undef IS
}

static int as_statement_assemble(as_context *a, const char *p, const char *e)
{
	p = as_whitespace_skip(p, e);
	if (p >= e) return 0;
	as_text w = as_text_first_word(p, e);
	int rc, lab = 0;
	if (w.s[0] == '.' && as_directive_conditional(a, w, w.s + w.n, e, &rc)) return rc;
	if (as_condition_is_ignored(a)) return 0;
	/* a quoted label: "name": (names with bytes outside ASCII, as clang writes them) */
	while (p < e && *p == '"') {
		const char *t = p;
		as_text qn;

		if (as_name_parse(a, &t, e, &qn)) return -1;
		t = as_whitespace_skip(t, e);
		if (t >= e || *t != ':') break;
		if (as_label_define(a, qn.s, qn.n)) return -1;
		p = as_whitespace_skip(t + 1, e);
		if (p >= e) return 0;
		w = as_text_first_word(p, e);
		lab = 1;
	}
	/* labels */
	const char *q;
	for (;;) {
		q = as_whitespace_skip(w.s + w.n, e);
		if (!w.n) break;
		if (q < e && *q == ':') {
			int numeric = 1;
			for (uint32_t i = 0; i < w.n; i++) if (!as_char_is_digit((unsigned char)w.s[i])) numeric = 0;
			if (!numeric && as_char_is_digit((unsigned char)w.s[0])) break;
			if (as_label_define(a, w.s, w.n)) return -1;
			p = as_whitespace_skip(q + 1, e);
			if (p >= e) return 0;
			w = as_text_first_word(p, e);
			lab = 1;
			continue;
		}
		break;
	}
	if (!w.n) return as_error_report(a, "unexpected token at start of statement");
	if (lab && w.s[0] == '.' && as_directive_conditional(a, w, w.s + w.n, e, &rc)) return rc;	/* "2: .if ..." */
	if (q < e && *q == '=' && !(q + 1 < e && q[1] == '=')) return as_symbol_assign(a, w, q + 1, e);
	if (q + 4 <= e && !memcmp(q, ".req", 4) && (q + 4 == e || !as_char_is_identifier((unsigned char)q[4]))) {
		as_text reg = as_text_trim(q + 4, e);
		if (a->alias_count == a->alias_capacity) {
			uint32_t nc = a->alias_capacity ? a->alias_capacity * 2 : 64;
			as_register_alias *t = as_region_alloc(&a->definitions, sizeof *t * nc);
			if (a->alias_count) memcpy(t, a->al, sizeof *t * a->alias_count);
			a->al = t; a->alias_capacity = nc;
		}
		a->al[a->alias_count].name = as_text_duplicate(a, w.s, w.n);
		a->al[a->alias_count].reg = as_text_duplicate(a, reg.s, reg.n);
		uint32_t h = as_alias_hash(w.s, w.n);
		a->al[a->alias_count].next = a->alias_buckets[h]; a->alias_buckets[h] = a->alias_count + 1;
		a->alias_count++;
		return 0;
	}
	as_macro *m = as_macro_find(a, w.s, w.n);
	if (m) return as_macro_invoke(a, m, w.s + w.n, e);
	if (w.s[0] == '.') return as_directive_dispatch(a, w, w.s + w.n, e);
	if (a->dwarf_row_is_pending) as_dwarf_row_emit(a);
	if (a->o->machine == EM_X86_64)
		return x64_as_instruction_assemble(a, w.s, e, w.n);
	return a64_as_instruction_assemble(a, w.s, e, w.n);
}

/* ---------------------------------------------------------------- driver */

as_context *as_new(void)
{
	as_context *a = as_region_reserve(sizeof *a);
	a->expansion_text.base = as_region_reserve((size_t)1 << 32); a->expansion_text.cap = (size_t)1 << 32;
	a->definitions.base = as_region_reserve((size_t)1 << 30); a->definitions.cap = (size_t)1 << 30;
	a->sb.base = as_region_reserve((size_t)1 << 24); a->sb.cap = (size_t)1 << 24;
	return a;
}

void as_statistics_get(const as_context *a, uint64_t *stmts, uint64_t *bytes) { *stmts = a->statement_count; *bytes = a->statement_bytes; }

void as_free(as_context *a)
{
	munmap(a->expansion_text.base, a->expansion_text.cap); munmap(a->definitions.base, a->definitions.cap); munmap(a->sb.base, a->sb.cap);
	munmap(a, sizeof *a);
}

void as_include_directory_set(as_context *a, const char *dir) { a->dir = dir; }

/*
 * A persistent assembler bound to one ElfObj for a whole TU: as_begin()
 * resets both, as_snippet_assemble() runs statements in the current section
 * (macros, .set variables, numeric labels and the section stack persist
 * across snippets, as in one source file), as_end() flushes literal pools
 * and resolves variables.  The text of every snippet must stay valid until
 * as_end() (macro bodies point into it).  as_run() is the three in one.
 */
void as_begin(as_context *a, ElfObj *o, const char *name)
{
	a->o = o; a->frame_count = 0; a->condition_count = 0; a->section_stack_count = 0; a->err = 0; a->msg[0] = 0; a->mlen = a->error_count = 0;
	a->expansion_text.used = 0; a->definitions.used = 0; a->ninst = 0; a->alias_count = 0; a->alias_capacity = 0; a->al = 0;
	memset(a->alias_buckets, 0, sizeof a->alias_buckets);
	a->file_name = name;
	a->ident_count = 0; a->end_seen = 0; a->altmacro_mode = 0; a->end_line = 0;
	a->cfi_instruction_count = a->cfi_instruction_capacity = a->frame_description_count = a->frame_description_capacity = 0; a->in_frame_description = 0; a->cfi_eh_frame_selected = 0; a->symver_count = a->symver_capacity = 0; a->addrsig_symbol_count = a->addrsig_symbol_capacity = 0;
	a->dwarf_file_count = a->dwarf_file_capacity = a->dwarf_directory_count = a->dwarf_directory_capacity = a->dwarf_row_count = a->dwarf_row_capacity = 0; a->variable_item_count = a->variable_item_capacity = 0; a->dwarf_row_is_pending = a->dwarf_table_is_due = 0;
	a->variable_rank = NULL;
	memset(&a->dwarf_row_pending, 0, sizeof a->dwarf_row_pending); a->dwarf_row_pending.flags = 1;
	a->macro_mask = 255; a->macro_count = 0;
	a->macro_table = as_region_alloc(&a->definitions, sizeof(as_macro *) * (a->macro_mask + 1));
	memset(a->macro_table, 0, sizeof(as_macro *) * (a->macro_mask + 1));
	a->literal_pool_count = 0; a->literal_pool_capacity = 0; a->pool = 0;
	a->variable_mask = 63; a->variable_count = 0;
	a->variables = as_region_alloc(&a->definitions, sizeof(as_variable) * (a->variable_mask + 1));
	memset(a->variables, 0, sizeof(as_variable) * (a->variable_mask + 1));
	a->numeric_label_mask = 255; a->numeric_label_count = 0;
	a->numeric_labels = as_region_alloc(&a->definitions, sizeof(as_numeric_label) * (a->numeric_label_mask + 1));
	memset(a->numeric_labels, 0, sizeof(as_numeric_label) * (a->numeric_label_mask + 1));
	elf_reset(o);
	uint32_t text_sec = as_section_standard_find(a, ".text");
	elf_sec(o, text_sec)->align = 2;
	a->cur = a->prev = text_sec;
}

int as_snippet_assemble(as_context *a, const char *text, size_t len)
{
	ElfObj *o = a->o;
	if (a->err) return -1;
	as_frame_push(a, text, len, AS_FRAME_FILE, 0);
	while (a->frame_count && !a->err) {
		as_frame *f = &a->frames[a->frame_count - 1];
		if (f->p >= f->e) { as_frame_pop(a); continue; }
		const char *raw, *s;
		size_t n = as_statement_next(a, f, &raw, &s);
		a->statement_count++; a->statement_bytes += n + 1;
		if (as_statement_assemble(a, s, s + n)) {
			size_t k = strlen(a->msg);
			k += (size_t)snprintf(a->msg + k, sizeof a->msg - k, "\n  in: %.*s", (int)(n > 200 ? 200 : n), s);
			/* the instantiations it is in, innermost first (llvm-mc's notes) */
			for (int i = a->frame_count - 1; i > 0 && a->frames[i].kind != AS_FRAME_FILE && k < sizeof a->msg; i--)
				k += (size_t)snprintf(a->msg + k, sizeof a->msg - k, "\n%s:%u: note: while in macro instantiation",
						      a->frames[i - 1].kind == AS_FRAME_FILE ? a->file_name : "<instantiation>", a->frames[i].caller_line);
			/* as llvm-mc: report it and go on with the next statement (at most 20) */
			a->mlen = (uint32_t)strlen(a->msg);
			if (++a->error_count >= 20 || a->mlen + 600 > sizeof a->msg) break;
			a->err = 0;
			continue;
		}
		if (o->has_err) { as_error_report(a, "%s", o->err); break; }
		if (a->end_seen) break;	/* .end: the rest of the input is ignored */
	}
	a->frame_count = 0;
	if (a->error_count) a->err = 1;
	return a->err ? -1 : 0;
}

int as_end(as_context *a)
{
	ElfObj *o = a->o;
	if (a->condition_count && (!a->err || a->error_count)) {	/* (after statement errors too, as llvm-mc) */
		a->err = 0;
		as_error_report(a, "unmatched .ifs or .elses");
	}
	if (!a->err && a->literal_pool_count) a64_as_pools_flush(a);
	if (!a->err) {
		/* undefined numeric forward references */
		for (uint32_t i = 0; i <= a->numeric_label_mask; i++)
			if (a->numeric_labels[i].fwd && !(elf_symp(o, a->numeric_labels[i].fwd)->flags & ESYM_DEFINED)) { as_error_report(a, "directional label undefined"); break; }
	}
	if (!a->err && a->variable_count) {
		for (uint32_t i = 0; i <= a->variable_mask; i++) {
			as_variable *vr = &a->variables[i];
			if (!vr->sym || vr->v.add == (uint32_t)-1) continue;
			ElfSym *s = elf_symp(o, vr->sym);
			if (s->flags & ESYM_TEMP) continue;
			as_value v = vr->v;
			as_value_fold(a, &v);
			if (v.add && !v.sub && (elf_symp(o, v.add)->flags & ESYM_DEFINED)) {
				ElfSym *t = elf_symp(o, v.add);
				elf_define(o, vr->sym, t->frag, t->value + (uint64_t)v.c);
				s->alias = t->alias ? t->alias : v.add;
			} else if (!v.add && !v.sub) elf_define_abs(o, vr->sym, (uint64_t)v.c);
			else { as_error_report(a, "unsupported variable '%s'", s->name); break; }
		}
	}
	if (!a->err && a->variable_item_count) as_variable_items_finish(a);
	if (!a->err && a->frame_description_count) as_cfi_frame_emit(a);
	if (!a->err && a->dwarf_table_is_due) as_dwarf_line_finish(a);
	if (!a->err && a->symver_count) as_symver_finish(a);
	if (!a->err && a->addrsig_symbol_count) {
		o->addrsig = a->addrsig_symbols;	/* in a->defs: lives until the next run, after the object is written */
		o->naddrsig = a->addrsig_symbol_count;
	}
	return a->err ? -1 : 0;
}

const char *as_message_get(const as_context *a) { return a->msg; }
uint32_t as_fragment_current(const as_context *a) { return a->cur; }
int as_is_failed(const as_context *a) { return a->err; }

/* a symbol as an operand names it (".L" names are temporary) */
uint32_t as_symbol_by_name(as_context *a, const char *name, size_t len) { return as_symbol_intern(a, name, len); }

/* sym + c as an operand expression: .set variables substituted, folded */
ElfVal as_symbol_value(as_context *a, uint32_t y, int64_t c)
{
	as_value v = { y, 0, 0 };
	if (a->variable_count) {
		as_variable *vr = as_variable_slot_find(a, y, 0);
		if (vr && vr->v.add != (uint32_t)-1) v = vr->v;
	}
	as_value_fold(a, &v);
	v.c += c;
	as_value_fold(a, &v);
	return v;
}

/* "y:" at the current position */
int as_label_define_symbol(as_context *a, uint32_t y)
{
	ElfSym *ys = elf_symp(a->o, y);
	if (ys->flags & ESYM_DEFINED) return as_error_report(a, "invalid symbol redefinition");
	elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
	return 0;
}

int as_run(as_context *a, ElfObj *o, const char *text, size_t len, const char *name, const char **msg)
{
	as_begin(a, o, name);
	if ((as_snippet_assemble(a, text, len) | as_end(a)) != 0) { *msg = a->msg; return -1; }
	return 0;
}
