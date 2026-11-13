/* sym.c: symbols of the object as the assembler names them: lookup, temporaries, numeric labels, .set variables,
 * the attribute directives (.globl, .type, .size ...), assignments, .symver and .weakref. */
#include "asimpl.h"

/* ---------------------------------------------------------------- symbols */

VPRIV uint32_t sym(As *a, const char *s, size_t n)
{
	uint32_t y = elf_sym(a->o, s, (uint32_t)n);
	ElfSym *p = elf_symp(a->o, y);
	if (!p->flags && n >= 2 && s[0] == '.' && s[1] == 'L') p->flags |= ESYM_TEMP;
	return y;
}

VPRIV uint32_t tmpsym(As *a)
{
	uint32_t y = elf_sym_anon(a->o, "", 0);
	elf_symp(a->o, y)->flags |= ESYM_TEMP;
	return y;
}

VPRIV uint32_t here(As *a)
{
	uint32_t y = tmpsym(a);
	elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
	return y;
}

VPRIV NumLab *numlab(As *a, uint32_t num)
{
	if (a->nlcount * 2 >= a->nlmask) {
		uint32_t nm = a->nlmask * 2 + 1;
		NumLab *t = rget(&a->defs, sizeof *t * (nm + 1));
		memset(t, 0, sizeof *t * (nm + 1));
		for (uint32_t i = 0; i <= a->nlmask; i++) {
			if (!a->nl[i].cur && !a->nl[i].fwd) continue;
			uint32_t j = (a->nl[i].num * 2654435761u) & nm;
			while (t[j].cur || t[j].fwd) j = (j + 1) & nm;
			t[j] = a->nl[i];
		}
		a->nl = t; a->nlmask = nm;
	}
	uint32_t j = (num * 2654435761u) & a->nlmask;
	while (a->nl[j].cur || a->nl[j].fwd) {
		if (a->nl[j].num == num) return &a->nl[j];
		j = (j + 1) & a->nlmask;
	}
	a->nl[j].num = num; a->nlcount++;
	return &a->nl[j];
}

VPRIV Var *var_slot(As *a, uint32_t y, int create)
{
	if (create && a->vcount * 2 >= a->vmask) {
		uint32_t nm = a->vmask * 2 + 1;
		Var *t = rget(&a->defs, sizeof *t * (nm + 1));
		memset(t, 0, sizeof *t * (nm + 1));
		for (uint32_t i = 0; i <= a->vmask; i++) {
			if (!a->vars[i].sym) continue;
			uint32_t j = (a->vars[i].sym * 2654435761u) & nm;
			while (t[j].sym) j = (j + 1) & nm;
			t[j] = a->vars[i];
		}
		a->vars = t; a->vmask = nm;
	}
	uint32_t j = (y * 2654435761u) & a->vmask;
	while (a->vars[j].sym) {
		if (a->vars[j].sym == y) return &a->vars[j];
		j = (j + 1) & a->vmask;
	}
	if (!create) return 0;
	a->vars[j].sym = y; a->vcount++;
	return &a->vars[j];
}

/* ---------------------------------------------------------------- symbols directives */

VPRIV int d_symattr(As *a, const char *p, const char *e, int what)
{
	for (;;) {
		Sv n;
		if (parse_name(a, &p, e, &n)) return -1;
		uint32_t y = sym(a, n.s, n.n);
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
		p = ws(p, e);
		if (p >= e) return 0;
		if (*p != ',') return err(a, "unexpected token in directive");
		p++;
	}
}

VPRIV int d_type(As *a, const char *p, const char *e)
{
	Sv n;
	if (parse_name(a, &p, e, &n)) return -1;
	uint32_t y = sym(a, n.s, n.n);
	p = ws(p, e);
	if (p < e && *p == ',') p = ws(p + 1, e);
	if (p < e && (*p == '%' || *p == '@')) p++;
	const char *s = p;
	while (p < e && isidc((unsigned char)*p)) p++;
	Sv t = { s, (uint32_t)(p - s) };
	int ty;
	if (sveq(t, "function") || sveq(t, "STT_FUNC")) ty = STT_FUNC;
	else if (sveq(t, "object") || sveq(t, "STT_OBJECT")) ty = STT_OBJECT;
	else if (sveq(t, "notype") || sveq(t, "STT_NOTYPE")) ty = STT_NOTYPE;
	else if (sveq(t, "tls_object") || sveq(t, "STT_TLS")) ty = STT_TLS;
	else if (sveq(t, "gnu_indirect_function") || sveq(t, "STT_GNU_IFUNC")) ty = STT_GNU_IFUNC;
	else if (sveq(t, "common") || sveq(t, "STT_COMMON")) ty = STT_OBJECT;	/* MC: STT_COMMON is written as OBJECT */
	else if (sveq(t, "gnu_unique_object")) { ty = STT_OBJECT; elf_bind(a->o, y, 10); }	/* STB_GNU_UNIQUE */
	else return err(a, "unsupported attribute in '.type' directive");
	elf_reg(a->o, y);
	elf_type(a->o, y, ty);
	return 0;
}

VPRIV int d_size(As *a, const char *p, const char *e)
{
	Sv n;
	if (parse_name(a, &p, e, &n)) return -1;
	uint32_t y = sym(a, n.s, n.n);
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected comma in '.size' directive");
	p++;
	Val v;
	if (val_expr(a, &p, e, &v)) return -1;
	fold(a, &v);
	if (is_abs(a, &v)) { elf_symp(a->o, y)->size = (uint64_t)v.c; elf_symp(a->o, y)->flags &= ~ESYM_SIZE_EXPR; }
	else elf_size_expr(a->o, y, v);
	return 0;
}

/* x86: the register sym was made an alias of, or NULL */
VPRIV const char *as_ralias(As *a, uint32_t y)
{
	for (uint32_t i = a->nralias; i-- > 0;)
		if (a->ralias[i].sym == y)
			return a->ralias[i].reg[0] ? a->ralias[i].reg : NULL;
	return NULL;
}

/* .symver orig, name@[@[@]]ver[, remove|local|hidden]: an alias defined at
   the end (MC's ELFObjectWriter::executePostLayoutBinding) */
VPRIV int d_symver(As *a, const char *p, const char *e)
{
	Sv o, v;
	if (parse_name(a, &p, e, &o)) return -1;
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected a comma");
	p = ws(p + 1, e);
	const char *q = p;
	while (q < e && *q != ',' && *q != ' ' && *q != '\t') q++;
	v = (Sv){ p, (size_t)(q - p) };
	const char *at = memchr(v.s, '@', v.n);
	if (!at) return err(a, "expected a '@' in the name");
	int rm = 0;
	p = ws(q, e);
	if (p < e && *p == ',') {
		Sv w = trim(p + 1, e);
		if (w.n == 6 && !memcmp(w.s, "remove", 6)) rm = 1;
		else if (!(w.n == 5 && !memcmp(w.s, "local", 5)) && !(w.n == 6 && !memcmp(w.s, "hidden", 6)))
			return err(a, "expected 'remove'");
	}
	uint32_t yo = sym(a, o.s, o.n);
	elf_reg(a->o, yo);
	uint32_t yv = sym(a, v.s, v.n);
	elf_reg(a->o, yv);
	if (a->nsv == a->svcap) {
		uint32_t nc = a->svcap ? a->svcap * 2 : 16;
		void *t = rget(&a->defs, sizeof *a->sv * nc);
		if (a->nsv) memcpy(t, a->sv, sizeof *a->sv * a->nsv);
		a->sv = t; a->svcap = nc;
	}
	a->sv[a->nsv].orig = yo; a->sv[a->nsv].ver = yv; a->sv[a->nsv].rm = rm;
	a->nsv++;
	return 0;
}

VPRIV void symver_finish(As *a)
{
	ElfObj *o = a->o;
	for (uint32_t i = 0; i < a->nsv; i++) {
		uint32_t yo = a->sv[i].orig, yv = a->sv[i].ver;
		ElfSym *s = elf_symp(o, yo), *t = elf_symp(o, yv);
		if (a->sv[i].rm == 2) {	/* .weakref yo -> yv */
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
		int def = (s->flags & ESYM_DEFINED) != 0, rm = (int)a->sv[i].rm;
		const char *at = memchr(t->name, '@', t->len);
		if (at && t->len - (size_t)(at - t->name) >= 3 && !memcmp(at, "@@@", 3)) {
			/* name@@@v: @@ when defined here, @ otherwise */
			size_t k = (size_t)(at - t->name);
			char *n = rget(&a->defs, t->len);
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
