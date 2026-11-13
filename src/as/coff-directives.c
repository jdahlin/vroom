/* coff-directives.c: the directives of a COFF object (x86_64-w64-mingw32), as LLVM MC's COFFAsmParser
 * and X86AsmParser take them: .def/.scl/.type/.endef, .section with COFF flags and COMDAT selection,
 * .linkonce, .secrel32, .secidx, .rva, .symidx, .weak (a weak external), .file "name", and the .seh_*
 * family, whose unwind information the object layer builds (platform/win/unwind.c: each directive
 * defines a temporary label here and passes it on).  ELF's own directives (.size, .hidden, .ident,
 * .previous, ...) are unknown for COFF, as in MC.  (.cfi_* is the common code's: call-frame-information.c
 * writes MC's COFF form of .eh_frame.)
 *
 * as_directive_dispatch calls as_directive_coff first for a COFF object (a->object->format); it returns
 * 1 when it took the directive (its result in *rc), 0 to let the common directives run.  The state is
 * the open .def (a->coff_symbol_definition), reset by as_begin. */
#include "as-internal.h"

static as_text as_coff_word(const char *p, const char *e)
{
	const char *s = p;
	while (p < e && (as_char_is_identifier((unsigned char)*p) || *p == '@')) p++;
	return (as_text){ s, (uint32_t)(p - s) };
}

static int as_coff_end_check(as_context *a, const char *p, const char *e)
{
	p = as_whitespace_skip(p, e);
	return p < e ? as_error_report(a, "unexpected token in directive") : 0;
}

/* a symbol name (MC's parseIdentifier: no '-'; quoted names too) */
static int as_coff_identifier(as_context *a, const char **pp, const char *e, as_text *out)
{
	const char *p = as_whitespace_skip(*pp, e);
	if (p < e && *p == '"') return as_name_parse(a, pp, e, out);
	as_text t = as_coff_word(p, e);
	if (!t.length || as_char_is_digit((unsigned char)*p)) return as_error_report(a, "expected identifier in directive");
	*out = t;
	*pp = p + t.length;
	return 0;
}

/* ---------------------------------------------------------------- .section */

static int as_coff_section_flags(as_context *a, const char *s, const char *e, uint32_t *out)
{
	enum { ALLOC = 1, CODE = 2, LOAD = 4, INIT = 8, SHARED = 16, NOLOAD = 32, NOREAD = 64, NOWRITE = 128, DISCARD = 256, INFO = 512 };
	unsigned f = 0;
	int ro_removed = 0;
	for (; s < e; s++) {
		switch (*s) {
		case 'a': break;
		case 'b':
			f |= ALLOC;
			if (f & INIT) return as_error_report(a, "conflicting section flags 'b' and 'd'.");
			f &= ~(unsigned)LOAD;
			break;
		case 'd':
			f |= INIT;
			if (f & ALLOC) return as_error_report(a, "conflicting section flags 'b' and 'd'.");
			f &= ~(unsigned)NOWRITE;
			if (!(f & NOLOAD)) f |= LOAD;
			break;
		case 'n': f |= NOLOAD; f &= ~(unsigned)LOAD; break;
		case 'D': f |= DISCARD; break;
		case 'r':
			ro_removed = 0;
			f |= NOWRITE;
			if (!(f & CODE)) f |= INIT;
			if (!(f & NOLOAD)) f |= LOAD;
			break;
		case 's':
			f |= SHARED | INIT;
			f &= ~(unsigned)NOWRITE;
			if (!(f & NOLOAD)) f |= LOAD;
			break;
		case 'w': f &= ~(unsigned)NOWRITE; ro_removed = 1; break;
		case 'x':
			f |= CODE;
			if (!(f & NOLOAD)) f |= LOAD;
			if (!ro_removed) f |= NOWRITE;
			break;
		case 'y': f |= NOREAD | NOWRITE; break;
		case 'i': f |= INFO; break;
		default: return as_error_report(a, "unknown flag");
		}
	}
	if (!f) f = INIT;
	uint32_t c = 0;
	if (f & CODE) c |= COFF_SCN_CODE | COFF_SCN_EXECUTE;
	if (f & INIT) c |= COFF_SCN_INIT_DATA;
	if ((f & ALLOC) && !(f & LOAD)) c |= COFF_SCN_UNINIT_DATA;
	if (f & NOLOAD) c |= COFF_SCN_LNK_REMOVE;
	if (f & DISCARD) c |= COFF_SCN_DISCARDABLE;
	if (!(f & NOREAD)) c |= COFF_SCN_READ;
	if (!(f & NOWRITE)) c |= COFF_SCN_WRITE;
	if (f & SHARED) c |= COFF_SCN_SHARED;
	if (f & INFO) c |= COFF_SCN_LNK_INFO;
	*out = c;
	return 0;
}

static int as_coff_selection(as_text t)
{
	static const char *const names[] = { "one_only", "discard", "same_size", "same_contents", "associative", "largest", "newest" };
	for (int i = 0; i < 7; i++)
		if (as_text_equals(t, names[i])) return i + 1;	/* IMAGE_COMDAT_SELECT_NODUPLICATES.. */
	return 0;
}

/* .section name[, "flags"[, selection, comdat-symbol]]; push: .pushsection */
static int as_coff_section(as_context *a, const char *p, const char *e, int push)
{
	as_text name;
	const char *q = as_whitespace_skip(p, e);
	if (q >= e || (*q != '"' && !as_char_is_identifier((unsigned char)*q) && *q != '@')) return as_error_report(a, "expected identifier in directive");
	if (as_name_parse(a, &p, e, &name)) return -1;
	uint32_t chars = COFF_SCN_INIT_DATA | COFF_SCN_READ | COFF_SCN_WRITE, comdat = 0;
	int selection = 0;
	int64_t unique_id = -1;
	p = as_whitespace_skip(p, e);
	if (p < e && *p == ',') {
		p = as_whitespace_skip(p + 1, e);
		if (p >= e || *p != '"') return as_error_report(a, "expected string in directive");
		const char *s = ++p;
		while (p < e && *p != '"') p++;
		if (p >= e) return as_error_report(a, "unterminated string");
		if (as_coff_section_flags(a, s, p, &chars)) return -1;
		if (name.length >= 6 && !memcmp(name.text, ".debug", 6)) chars |= COFF_SCN_DISCARDABLE;	/* (MC: implicitly) */
		p = as_whitespace_skip(p + 1, e);
	}
	if (p < e && *p == ',') {
		p = as_whitespace_skip(p + 1, e);
		as_text t = as_coff_word(p, e);
		if (as_text_equals(t, "unique")) goto unique;
		if (!t.length || !as_char_is_identifier_start((unsigned char)*p))
			return as_error_report(a, "expected comdat type such as 'discard' or 'largest' after protection bits");
		selection = as_coff_selection(t);
		if (!selection) return as_error_report(a, "unrecognized COMDAT type '%.*s'", (int)t.length, t.text);
		chars |= COFF_SCN_LNK_COMDAT;
		p = as_whitespace_skip(p + t.length, e);
		if (p >= e || *p != ',') return as_error_report(a, "expected comma in directive");
		as_text g;
		p++;
		if (as_whitespace_skip(p, e) >= e || as_name_parse(a, &p, e, &g)) return as_error_report(a, "expected identifier in directive");
		comdat = as_symbol_intern(a, g.text, g.length);
		p = as_whitespace_skip(p, e);
		if (p < e && *p == ',') {
			p = as_whitespace_skip(p + 1, e);
			if (!as_text_equals(as_coff_word(p, e), "unique")) return as_error_report(a, "expected 'unique'");
		unique:
			p = as_whitespace_skip(p + 6, e);
			if (p >= e || *p != ',') return as_error_report(a, "expected comma");
			p++;
			if (as_expression_absolute_parse(a, &p, e, &unique_id)) return -1;
			if (unique_id < 0 || unique_id >= (int64_t)UINT32_MAX) return as_error_report(a, "unique id must be positive");
		}
	}
	if (as_coff_end_check(a, p, e)) return -1;
	if (push) {
		if (a->section_stack_count >= AS_SECTION_STACK_MAX) return as_error_report(a, "section stack overflow");
		a->section_stack[a->section_stack_count].cur = a->fragment_current; a->section_stack[a->section_stack_count].prev = a->fragment_previous; a->section_stack_count++;
	}
	uint32_t s = unique_id >= 0 ? coff_section_unique(a->object, name.text, name.length, chars, comdat, selection, (uint32_t)unique_id)
				    : coff_section(a->object, name.text, name.length, chars, comdat, selection);
	if (comdat) elf_reg(a->object, comdat);
	as_fragment_switch(a, s);
	return 0;
}

static int as_coff_linkonce(as_context *a, const char *p, const char *e)
{
	int selection = COFF_SELECT_ANY;
	p = as_whitespace_skip(p, e);
	if (p < e && as_char_is_identifier_start((unsigned char)*p)) {
		as_text t = as_coff_word(p, e);
		selection = as_coff_selection(t);
		if (!selection) return as_error_report(a, "unrecognized COMDAT type '%.*s'", (int)t.length, t.text);
		p += t.length;
	}
	ElfSec *se = elf_sec(a->object, elf_frag(a->object, a->fragment_current)->sec);
	if (selection == COFF_SELECT_ASSOCIATIVE) return as_error_report(a, "cannot make section associative with .linkonce");
	if (se->flags & COFF_SCN_LNK_COMDAT) return as_error_report(a, "section '%s' is already linkonce", se->name);
	if (as_coff_end_check(a, p, e)) return -1;
	se->comdat = (uint8_t)selection;
	se->flags |= COFF_SCN_LNK_COMDAT;
	return 0;
}

/* ---------------------------------------------------------------- .def .scl .type .endef */

static int as_coff_symbol_definition(as_context *a, char what, const char *p, const char *e)
{
	ElfObj *o = a->object;
	if (what == 'd') {
		as_text n;
		if (as_coff_identifier(a, &p, e, &n)) return -1;
		uint32_t y = as_symbol_intern(a, n.text, n.length), open = a->coff_symbol_definition;
		a->coff_symbol_definition = y;
		if (open) return as_error_report(a, "starting a new symbol definition without completing the previous one");
		return 0;
	}
	if (what == 'e') {
		if (!a->coff_symbol_definition) return as_error_report(a, "ending symbol definition without starting one");
		a->coff_symbol_definition = 0;
		return 0;
	}
	int64_t v;
	if (as_expression_absolute_parse(a, &p, e, &v)) return -1;
	if (as_coff_end_check(a, p, e)) return -1;
	uint32_t y = a->coff_symbol_definition;
	if (what == 's') {
		if (!y) return as_error_report(a, "storage class specified outside of symbol definition");
		if (v & ~(int64_t)0xff) return as_error_report(a, "storage class value '%lld' out of range", (long long)v);
		elf_reg(o, y);
		elf_symp(o, y)->coff_class = (uint8_t)v;
		return 0;
	}
	if (!y) return as_error_report(a, "symbol type specified outside of a symbol definition");
	if (v & ~(int64_t)0xffff) return as_error_report(a, "type value '%lld' out of range", (long long)v);
	elf_reg(o, y);
	elf_symp(o, y)->coff_type = (uint16_t)v;
	return 0;
}

/* ---------------------------------------------------------------- symbol references */

/* .secrel32 sym[+off], .secidx sym, .symidx sym: one symbol, a relocation of kind type */
static int as_coff_symbol_reference(as_context *a, const char *p, const char *e, int size, uint32_t type, int offset_allowed)
{
	as_text n;
	if (as_coff_identifier(a, &p, e, &n)) return -1;
	int64_t off = 0;
	p = as_whitespace_skip(p, e);
	if (offset_allowed && p < e && *p == '+') {
		if (as_expression_absolute_parse(a, &p, e, &off)) return -1;
		if (off < 0 || off > (int64_t)UINT32_MAX)
			return as_error_report(a, "invalid '.secrel32' directive offset, can't be less than zero or greater than std::numeric_limits<uint32_t>::max()");
	}
	if (as_coff_end_check(a, p, e)) return -1;
	as_value v = { as_symbol_intern(a, n.text, n.length), 0, off };
	elf_reg(a->object, v.add);
	return as_value_emit_relocation(a, size, &v, type);
}

/* .rva sym[(+|-)off], ...: image-relative (ADDR32NB) */
static int as_coff_rva(as_context *a, const char *p, const char *e)
{
	for (;;) {
		as_text n;
		if (as_coff_identifier(a, &p, e, &n)) return -1;
		int64_t off = 0;
		p = as_whitespace_skip(p, e);
		if (p < e && (*p == '+' || *p == '-')) {
			if (as_expression_absolute_parse(a, &p, e, &off)) return -1;
			if (off < INT32_MIN || off > INT32_MAX)
				return as_error_report(a, "invalid '.rva' directive offset, can't be less than -2147483648 or greater than 2147483647");
		}
		as_value v = { as_symbol_intern(a, n.text, n.length), 0, off };
		elf_reg(a->object, v.add);
		if (as_value_emit_relocation(a, 4, &v, R_VCC_COFF_IMGREL32)) return -1;
		p = as_whitespace_skip(p, e);
		if (p >= e) return 0;
		if (*p != ',') return as_error_report(a, "unexpected token in directive");
		p++;
	}
}

/* .weak / .weak_anti_dep name, ...: a weak external */
static int as_coff_weak(as_context *a, const char *p, const char *e, int characteristics)
{
	for (;;) {
		as_text n;
		if (as_name_parse(a, &p, e, &n)) return -1;
		uint32_t y = as_symbol_intern(a, n.text, n.length);
		elf_reg(a->object, y);
		elf_bind(a->object, y, STB_WEAK);
		elf_symp(a->object, y)->coff_weak = (uint8_t)characteristics;
		p = as_whitespace_skip(p, e);
		if (p >= e) return 0;
		if (*p != ',') return as_error_report(a, "unexpected token in directive");
		p++;
	}
}

/* ---------------------------------------------------------------- .seh_* */

/* %reg or its number: a general register (xmm = 0) or an XMM register */
static int as_coff_seh_register(as_context *a, const char **pp, const char *e, int xmm, unsigned *reg)
{
	static const char *const gpr[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi" };
	const char *p = as_whitespace_skip(*pp, e);
	if (p < e && as_char_is_digit((unsigned char)*p)) {
		int64_t v;
		if (as_expression_absolute_parse(a, &p, e, &v)) return -1;
		if (v < 0 || v > 31) return as_error_report(a, "incorrect register number for use with this directive");
		*reg = (unsigned)v; *pp = p;
		return 0;
	}
	if (p < e && *p == '%') p++;
	as_text t = as_coff_word(p, e);
	char n[8] = { 0 };
	for (uint32_t i = 0; i < t.length && i < 7; i++) n[i] = (char)as_char_to_lower((unsigned char)t.text[i]);
	int r = -1, cls = -1;	/* cls 0 a 64-bit GPR, 1 an XMM register, 2 another register */
	for (int i = 0; i < 8; i++) if (!strcmp(n, gpr[i])) r = i, cls = 0;
	if (n[0] == 'r' && as_char_is_digit((unsigned char)n[1])) {
		int k = atoi(n + 1);
		size_t dl = strspn(n + 1, "0123456789");
		if (k >= 8 && k <= 31) r = k, cls = n[1 + dl] ? 2 : 0;
	}
	if (!strncmp(n, "xmm", 3) && as_char_is_digit((unsigned char)n[3])) { int k = atoi(n + 3); if (k <= 31) r = k, cls = 1; }
	if (cls < 0 && t.length && t.length < 6) cls = 2;	/* eax, ymm0, ...: a register of another class */
	if (r < 0 && cls != 2) return as_error_report(a, "invalid register name");
	if (cls != xmm) return as_error_report(a, "register is not supported for use with this directive");
	*reg = (unsigned)r; *pp = p + t.length;
	return 0;
}

static int as_coff_seh_result(as_context *a, const char *message)
{
	return message ? as_error_report(a, "%s", message) : 0;
}

/* d: the directive after ".seh_" */
static int as_coff_seh(as_context *a, as_text d, const char *p, const char *e)
{
	ElfObj *o = a->object;
	const char *bad;
#define SEH_IS(x) (d.length == sizeof(x) - 1 && !memcmp(d.text, x, d.length))
	if (SEH_IS("proc")) {
		as_text n;
		if (as_coff_identifier(a, &p, e, &n)) return -1;
		if (as_coff_end_check(a, p, e)) return -1;
		uint32_t y = as_symbol_intern(a, n.text, n.length);
		if (coff_seh_open(o)) return as_error_report(a, "Starting a function before ending the previous one!");
		return as_coff_seh_result(a, coff_seh_proc(o, y, as_label_temporary_define(a)));
	}
	if (SEH_IS("handler")) {
		as_text n;
		int unwind = 0, except = 0;
		if (as_coff_identifier(a, &p, e, &n)) return -1;
		p = as_whitespace_skip(p, e);
		if (p >= e || *p != ',') return as_error_report(a, "you must specify one or both of @unwind or @except");
		for (int k = 0; k < 2; k++) {
			p = as_whitespace_skip(p + 1, e);
			if (p >= e || (*p != '@' && *p != '%')) return as_error_report(a, "a handler attribute must begin with '@' or '%%'");
			as_text t = as_coff_word(p + 1, e);
			if (as_text_equals(t, "unwind")) unwind = 1;
			else if (as_text_equals(t, "except")) except = 1;
			else return as_error_report(a, "expected @unwind or @except");
			p = as_whitespace_skip(p + 1 + t.length, e);
			if (p >= e || *p != ',') break;
		}
		if (as_coff_end_check(a, p, e)) return -1;
		return as_coff_seh_result(a, coff_seh_handler(o, as_symbol_intern(a, n.text, n.length), unwind, except));
	}
	if ((SEH_IS("endproc") || SEH_IS("endprologue") || SEH_IS("handlerdata") || SEH_IS("startepilogue") || SEH_IS("endepilogue") ||
	     SEH_IS("endfunclet") || SEH_IS("unwindv2start") || SEH_IS("splitchained")) && as_coff_end_check(a, p, e))
		return -1;
	if ((bad = coff_seh_check(o))) return as_error_report(a, "%s", bad);
	if (SEH_IS("endproc")) {
		uint32_t s;
		if ((bad = coff_seh_end(o, as_label_temporary_define(a), &s))) return as_error_report(a, "%s", bad);
		as_fragment_switch(a, s);
		return 0;
	}
	if (SEH_IS("handlerdata")) {
		uint32_t s;
		if ((bad = coff_seh_handler_data(o, &s))) return as_error_report(a, "%s", bad);
		as_fragment_switch(a, s);
		return 0;
	}
	if (SEH_IS("endprologue")) return as_coff_seh_result(a, coff_seh_end_prologue(o, as_label_temporary_define(a)));
	if (SEH_IS("startepilogue") || SEH_IS("endepilogue"))
		return as_coff_seh_result(a, coff_seh_epilogue(o, SEH_IS("startepilogue"), as_label_temporary_define(a)));
	if (SEH_IS("endfunclet") || SEH_IS("unwindv2start")) return 0;	/* (nothing in version 1 for x86-64) */
	if (SEH_IS("unwindversion")) {
		int64_t v;
		if (as_expression_absolute_parse(a, &p, e, &v) || as_coff_end_check(a, p, e)) return -1;
		if (v == 1) return 0;
		if (v == 2) return as_error_report(a, "unsupported: .seh_unwindversion 2 (vcc writes UNWIND_INFO version 1)");
		return as_error_report(a, "Unsupported version specified in .seh_unwindversion");
	}
	if (SEH_IS("splitchained")) return as_error_report(a, "unsupported: .seh_splitchained (chained unwind information)");
	if (SEH_IS("stackalloc")) {
		int64_t v;
		if (as_expression_absolute_parse(a, &p, e, &v) || as_coff_end_check(a, p, e)) return -1;
		return as_coff_seh_result(a, coff_seh_alloc(o, as_label_temporary_define(a), (uint64_t)(uint32_t)v));
	}
	if (SEH_IS("pushframe")) {
		int code = 0;
		p = as_whitespace_skip(p, e);
		if (p < e && *p == '@') {
			as_text t = as_coff_word(p + 1, e);
			if (!as_text_equals(t, "code")) return as_error_report(a, "expected @code");
			code = 1;
			p += 1 + t.length;
		}
		if (as_coff_end_check(a, p, e)) return -1;
		return as_coff_seh_result(a, coff_seh_push_frame(o, as_label_temporary_define(a), code));
	}
	int xmm = SEH_IS("savexmm");
	if (SEH_IS("pushreg") || SEH_IS("setframe") || SEH_IS("savereg") || xmm) {
		unsigned reg;
		int64_t off = 0;
		if (as_coff_seh_register(a, &p, e, xmm, &reg)) return -1;
		if (!SEH_IS("pushreg")) {
			p = as_whitespace_skip(p, e);
			if (p >= e || *p != ',')
				return as_error_report(a, SEH_IS("setframe") ? "you must specify a stack pointer offset" : "you must specify an offset on the stack");
			p++;
			if (as_expression_absolute_parse(a, &p, e, &off)) return -1;
		}
		if (as_coff_end_check(a, p, e)) return -1;
		if (!xmm && reg >= 16)	/* (APX r16-r31: UNWIND_INFO version 3, MC) */
			return as_error_report(a, ".seh_%.*s with an extended register requires unwind v3", (int)d.length, d.text);
		uint32_t label = as_label_temporary_define(a);
		if (SEH_IS("pushreg")) return as_coff_seh_result(a, coff_seh_push_reg(o, label, reg));
		if (SEH_IS("setframe")) return as_coff_seh_result(a, coff_seh_set_frame(o, label, reg, (uint64_t)(uint32_t)off));
		if (SEH_IS("savereg")) return as_coff_seh_result(a, coff_seh_save_reg(o, label, reg, (uint64_t)(uint32_t)off));
		return as_coff_seh_result(a, coff_seh_save_xmm(o, label, reg, (uint64_t)(uint32_t)off));
	}
#undef SEH_IS
	return as_error_report(a, "unknown directive");
}

/* ---------------------------------------------------------------- dispatch */

VPRIV int as_directive_coff(as_context *a, as_text w, const char *p, const char *e, int *rc)
{
	const char *d = w.text + 1;
	size_t n = w.length - 1;
#define IS(x) (n == sizeof(x) - 1 && !memcmp(d, x, n))
	if (n > 4 && !memcmp(d, "seh_", 4)) { *rc = as_coff_seh(a, (as_text){ d + 4, (uint32_t)(n - 4) }, p, e); return 1; }
	if (IS("def")) { *rc = as_coff_symbol_definition(a, 'd', p, e); return 1; }
	if (IS("scl")) { *rc = as_coff_symbol_definition(a, 's', p, e); return 1; }
	if (IS("type")) { *rc = as_coff_symbol_definition(a, 't', p, e); return 1; }
	if (IS("endef")) { *rc = as_coff_symbol_definition(a, 'e', p, e); return 1; }
	if (IS("section")) { *rc = as_coff_section(a, p, e, 0); return 1; }
	if (IS("pushsection")) { *rc = as_coff_section(a, p, e, 1); return 1; }
	if (IS("linkonce")) { *rc = as_coff_linkonce(a, p, e); return 1; }
	if (IS("secrel32")) { *rc = as_coff_symbol_reference(a, p, e, 4, R_VCC_COFF_SECREL32, 1); return 1; }
	if (IS("secidx")) { *rc = as_coff_symbol_reference(a, p, e, 2, R_VCC_COFF_SECTION, 0); return 1; }
	if (IS("symidx")) { *rc = as_coff_symbol_reference(a, p, e, 4, R_VCC_COFF_SYMIDX, 0); return 1; }
	if (IS("rva")) { *rc = as_coff_rva(a, p, e); return 1; }
	if (IS("weak")) { *rc = as_coff_weak(a, p, e, 3); return 1; }	/* IMAGE_WEAK_EXTERN_SEARCH_ALIAS */
	if (IS("weak_anti_dep")) { *rc = as_coff_weak(a, p, e, 4); return 1; }	/* IMAGE_WEAK_EXTERN_ANTI_DEPENDENCY */
	if (IS("file")) {
		const char *q = as_whitespace_skip(p, e);
		if (q < e && *q == '"') {	/* .file "name": a .file symbol (.file N "name" is DWARF's) */
			char buf[4096];
			size_t k;
			if ((size_t)(e - q) > sizeof buf) { *rc = as_error_report(a, "string too long"); return 1; }
			if (as_string_parse(a, &q, e, buf, &k)) { *rc = -1; return 1; }
			coff_file_name(a->object, buf, (uint32_t)k);
			*rc = 0;
			return 1;
		}
		return 0;
	}
	/* ELF's directives: unknown in MC's COFF parser */
	if (IS("size") || IS("hidden") || IS("protected") || IS("internal") || IS("local") || IS("symver") || IS("weakref") ||
	    IS("ident") || IS("subsection") || IS("previous") || IS("rodata") || IS("tlsdesccall") || IS("variant_pcs") || IS("inst") ||
	    IS("ltorg") || IS("pool")) {
		*rc = as_error_report(a, "unknown directive");
		return 1;
	}
#undef IS
	return 0;
}
