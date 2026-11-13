/* symbol.c: the assembler's symbols: lookup and creation by name, temporaries (.L names and the
 * assembler's own), numeric labels (1: 1b 1f), .set/.equ variables, the attribute directives (.globl,
 * .weak, .hidden, .type, .size, .comm's relatives), .symver, .weakref and .addrsig_sym.
 *
 * Symbols live in the ElfObj; this file only keeps the assembler's view: the numeric-label table and
 * the variable table (a .set whose value is not absolute is substituted where it is read and
 * materialised at as_end), both in the context's definitions region.  Redefinition and a bad attribute
 * fail the assembly.  .symver is applied at the end (as_symver_finish) because the versioned name may
 * be defined after it. */
#include "as-internal.h"

/* ---------------------------------------------------------------- symbols */

VPRIV uint32_t as_symbol_intern(as_context *a, const char *s, size_t n)
{
	uint32_t y = elf_sym(a->o, s, (uint32_t)n);
	ElfSym *p = elf_symp(a->o, y);
	if (!p->flags && n >= 2 && s[0] == '.' && s[1] == 'L') p->flags |= ESYM_TEMP;
	return y;
}

VPRIV uint32_t as_symbol_temporary(as_context *a)
{
	uint32_t y = elf_sym_anon(a->o, "", 0);
	elf_symp(a->o, y)->flags |= ESYM_TEMP;
	return y;
}

VPRIV uint32_t as_label_temporary_define(as_context *a)
{
	uint32_t y = as_symbol_temporary(a);
	elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
	return y;
}

VPRIV as_numeric_label *as_numeric_label_find(as_context *a, uint32_t num)
{
	if (a->numeric_label_count * 2 >= a->numeric_label_mask) {
		uint32_t nm = a->numeric_label_mask * 2 + 1;
		as_numeric_label *t = as_region_alloc(&a->definitions, sizeof *t * (nm + 1));
		memset(t, 0, sizeof *t * (nm + 1));
		for (uint32_t i = 0; i <= a->numeric_label_mask; i++) {
			if (!a->numeric_labels[i].cur && !a->numeric_labels[i].fwd) continue;
			uint32_t j = (a->numeric_labels[i].number * 2654435761u) & nm;
			while (t[j].cur || t[j].fwd) j = (j + 1) & nm;
			t[j] = a->numeric_labels[i];
		}
		a->numeric_labels = t; a->numeric_label_mask = nm;
	}
	uint32_t j = (num * 2654435761u) & a->numeric_label_mask;
	while (a->numeric_labels[j].cur || a->numeric_labels[j].fwd) {
		if (a->numeric_labels[j].number == num) return &a->numeric_labels[j];
		j = (j + 1) & a->numeric_label_mask;
	}
	a->numeric_labels[j].number = num; a->numeric_label_count++;
	return &a->numeric_labels[j];
}

VPRIV as_variable *as_variable_slot_find(as_context *a, uint32_t y, int create)
{
	if (create && a->variable_count * 2 >= a->variable_mask) {
		uint32_t nm = a->variable_mask * 2 + 1;
		as_variable *t = as_region_alloc(&a->definitions, sizeof *t * (nm + 1));
		memset(t, 0, sizeof *t * (nm + 1));
		for (uint32_t i = 0; i <= a->variable_mask; i++) {
			if (!a->variables[i].sym) continue;
			uint32_t j = (a->variables[i].sym * 2654435761u) & nm;
			while (t[j].sym) j = (j + 1) & nm;
			t[j] = a->variables[i];
		}
		a->variables = t; a->variable_mask = nm;
	}
	uint32_t j = (y * 2654435761u) & a->variable_mask;
	while (a->variables[j].sym) {
		if (a->variables[j].sym == y) return &a->variables[j];
		j = (j + 1) & a->variable_mask;
	}
	if (!create) return 0;
	a->variables[j].sym = y; a->variable_count++;
	return &a->variables[j];
}

/* ---------------------------------------------------------------- symbols directives */

VPRIV int as_directive_symbol_attribute(as_context *a, const char *p, const char *e, int what)
{
	for (;;) {
		as_text n;
		if (as_name_parse(a, &p, e, &n)) return -1;
		uint32_t y = as_symbol_intern(a, n.s, n.n);
		if (what != 'e') elf_reg(a->o, y);
		ElfSym *s = elf_symp(a->o, y);
		switch (what) {
		case 'g': elf_bind(a->o, y, STB_GLOBAL); break;
		case 'w': elf_bind(a->o, y, STB_WEAK); break;
		case 'l': elf_bind(a->o, y, STB_LOCAL); break;
		case 'h': elf_vis(a->o, y, STV_HIDDEN); if (!(s->flags & ESYM_BINDSET) && 0) {} break;
		case 'p': elf_vis(a->o, y, STV_PROTECTED); break;
		case 'i': elf_vis(a->o, y, STV_INTERNAL); break;
		case 'e': break;
		}
		if (what != 'e' && what != 'l') s->flags &= ~ESYM_TEMP;	/* .globl .Lx makes it real */
		p = as_whitespace_skip(p, e);
		if (p >= e) return 0;
		if (*p != ',') return as_error_report(a, "unexpected token in directive");
		p++;
	}
}

VPRIV int as_directive_type(as_context *a, const char *p, const char *e)
{
	as_text n;
	if (as_name_parse(a, &p, e, &n)) return -1;
	uint32_t y = as_symbol_intern(a, n.s, n.n);
	p = as_whitespace_skip(p, e);
	if (p < e && *p == ',') p = as_whitespace_skip(p + 1, e);
	if (p < e && (*p == '%' || *p == '@')) p++;
	const char *s = p;
	while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
	as_text t = { s, (uint32_t)(p - s) };
	int ty;
	if (as_text_equals(t, "function") || as_text_equals(t, "STT_FUNC")) ty = STT_FUNC;
	else if (as_text_equals(t, "object") || as_text_equals(t, "STT_OBJECT")) ty = STT_OBJECT;
	else if (as_text_equals(t, "notype") || as_text_equals(t, "STT_NOTYPE")) ty = STT_NOTYPE;
	else if (as_text_equals(t, "tls_object") || as_text_equals(t, "STT_TLS")) ty = STT_TLS;
	else if (as_text_equals(t, "gnu_indirect_function") || as_text_equals(t, "STT_GNU_IFUNC")) ty = STT_GNU_IFUNC;
	else if (as_text_equals(t, "common") || as_text_equals(t, "STT_COMMON")) ty = STT_OBJECT;	/* MC: STT_COMMON is written as OBJECT */
	else if (as_text_equals(t, "gnu_unique_object")) { ty = STT_OBJECT; elf_bind(a->o, y, 10); }	/* STB_GNU_UNIQUE */
	else return as_error_report(a, "unsupported attribute in '.type' directive");
	elf_reg(a->o, y);
	elf_type(a->o, y, ty);
	return 0;
}

VPRIV int as_directive_size(as_context *a, const char *p, const char *e)
{
	as_text n;
	if (as_name_parse(a, &p, e, &n)) return -1;
	uint32_t y = as_symbol_intern(a, n.s, n.n);
	p = as_whitespace_skip(p, e);
	if (p >= e || *p != ',') return as_error_report(a, "expected comma in '.size' directive");
	p++;
	as_value v;
	if (as_expression_value_parse(a, &p, e, &v)) return -1;
	as_value_fold(a, &v);
	if (as_value_is_absolute(a, &v)) { elf_symp(a->o, y)->size = (uint64_t)v.c; elf_symp(a->o, y)->flags &= ~ESYM_SIZE_EXPR; }
	else elf_size_expr(a->o, y, v);
	return 0;
}

/* x86: the register sym was made an alias of, or NULL */
VPRIV const char *as_register_alias_find(as_context *a, uint32_t y)
{
	for (uint32_t i = a->register_alias_count; i-- > 0;)
		if (a->register_aliases[i].sym == y)
			return a->register_aliases[i].reg[0] ? a->register_aliases[i].reg : NULL;
	return NULL;
}

/* .symver orig, name@[@[@]]ver[, remove|local|hidden]: an alias defined at
   the end (MC's ELFObjectWriter::executePostLayoutBinding) */
VPRIV int as_directive_symver(as_context *a, const char *p, const char *e)
{
	as_text o, v;
	if (as_name_parse(a, &p, e, &o)) return -1;
	p = as_whitespace_skip(p, e);
	if (p >= e || *p != ',') return as_error_report(a, "expected a comma");
	p = as_whitespace_skip(p + 1, e);
	const char *q = p;
	while (q < e && *q != ',' && *q != ' ' && *q != '\t') q++;
	v = (as_text){ p, (size_t)(q - p) };
	const char *at = memchr(v.s, '@', v.n);
	if (!at) return as_error_report(a, "expected a '@' in the name");
	int rm = 0;
	p = as_whitespace_skip(q, e);
	if (p < e && *p == ',') {
		as_text w = as_text_trim(p + 1, e);
		if (w.n == 6 && !memcmp(w.s, "remove", 6)) rm = 1;
		else if (!(w.n == 5 && !memcmp(w.s, "local", 5)) && !(w.n == 6 && !memcmp(w.s, "hidden", 6)))
			return as_error_report(a, "expected 'remove'");
	}
	uint32_t yo = as_symbol_intern(a, o.s, o.n);
	elf_reg(a->o, yo);
	uint32_t yv = as_symbol_intern(a, v.s, v.n);
	elf_reg(a->o, yv);
	if (a->symver_count == a->symver_capacity) {
		uint32_t nc = a->symver_capacity ? a->symver_capacity * 2 : 16;
		void *t = as_region_alloc(&a->definitions, sizeof *a->symvers * nc);
		if (a->symver_count) memcpy(t, a->symvers, sizeof *a->symvers * a->symver_count);
		a->symvers = t; a->symver_capacity = nc;
	}
	a->symvers[a->symver_count].orig = yo; a->symvers[a->symver_count].ver = yv; a->symvers[a->symver_count].rm = rm;
	a->symver_count++;
	return 0;
}

VPRIV void as_symver_finish(as_context *a)
{
	ElfObj *o = a->o;
	for (uint32_t i = 0; i < a->symver_count; i++) {
		uint32_t yo = a->symvers[i].orig, yv = a->symvers[i].ver;
		ElfSym *s = elf_symp(o, yo), *t = elf_symp(o, yv);
		if (a->symvers[i].rm == 2) {	/* .weakref yo -> yv */
			if (t->flags & ESYM_DEFINED) {
				elf_define(o, yo, t->frag, t->value);
				s->flags |= (t->flags & ESYM_ABS) | ESYM_NOSYMTAB;
				s->flags &= (uint8_t)~ESYM_USED;
				s->alias = t->alias ? t->alias : yv;
				continue;
			}
			/* only referenced through the alias: weak */
			if (!t->reg && !(t->flags & ESYM_BINDSET)) t->bind = STB_WEAK;
			ElfFix *fx = (ElfFix *)o->fixes.base;
			for (uint32_t k = 0; k < o->nfix; k++) {
				if (fx[k].v.add == yo) { fx[k].v.add = yv; elf_reg(o, yv); }
				if (fx[k].v.sub == yo) { fx[k].v.sub = yv; elf_reg(o, yv); }
			}
			s->flags = (uint8_t)((s->flags & ~ESYM_USED) | ESYM_NOSYMTAB);
			continue;
		}
		int def = (s->flags & ESYM_DEFINED) != 0, rm = (int)a->symvers[i].rm;
		const char *at = memchr(t->name, '@', t->len);
		if (at && t->len - (size_t)(at - t->name) >= 3 && !memcmp(at, "@@@", 3)) {
			/* name@@@v: @@ when defined here, @ otherwise */
			size_t k = (size_t)(at - t->name);
			char *n = as_region_alloc(&a->definitions, t->len);
			memcpy(n, t->name, k + 1);
			if (def) { memcpy(n + k + 1, at + 2, t->len - k - 2); }
			else { memcpy(n + k + 1, at + 3, t->len - k - 3); }
			t->name = n; t->len -= def ? 1 : 2;
			rm = 1;
		}
		if (def) {
			elf_define(o, yv, s->frag, s->value);
			t->flags |= s->flags & ESYM_ABS;
			t->bind = s->bind; t->type = s->type; t->vis = s->vis;
			t->alias = s->alias ? s->alias : yo;
			t->flags |= ESYM_BINDSET;
		}
		if (!def || rm) {
			/* references go to the versioned name; the original disappears */
			ElfFix *fx = (ElfFix *)o->fixes.base;
			for (uint32_t k = 0; k < o->nfix; k++) {
				if (fx[k].v.add == yo) fx[k].v.add = yv;
				if (fx[k].v.sub == yo) fx[k].v.sub = yv;
			}
			if (s->flags & ESYM_USED) t->flags |= ESYM_USED;
			s->flags = (uint8_t)((s->flags & ~ESYM_USED) | ESYM_NOSYMTAB);
		}
	}
}
