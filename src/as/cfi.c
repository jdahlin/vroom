/* cfi.c: the .cfi_* directives, and .eh_frame as MC's FrameEmitterImpl writes it. */
#include "asimpl.h"

/* ---------------------------------------------------------------- CFI */

enum { CF_DEF_CFA, CF_DEF_CFA_OFFSET, CF_DEF_CFA_REGISTER, CF_ADJUST, CF_OFFSET, CF_REL_OFFSET,
       CF_RESTORE, CF_UNDEFINED, CF_SAME, CF_REGISTER, CF_REMEMBER, CF_RESTORE_STATE, CF_ESCAPE,
       CF_NEGATE_RA };

/* a DWARF register: x0-x30/w0-w30, sp/wsp, fp, lr, v/b/h/s/d/q 0-31 (64+), or a number */
/* x86-64's DWARF register numbers (the SysV psABI's) */
static int cfi_reg86(const char *s, size_t n)
{
	static const char *const nm[17] = { "rax", "rdx", "rcx", "rbx", "rsi", "rdi", "rbp", "rsp",
					    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rip" };
	char b[8];

	if (n >= sizeof b)
		return -1;
	for (size_t i = 0; i < n; i++)
		b[i] = (char)c_lower((unsigned char)s[i]);
	b[n] = 0;
	for (int i = 0; i < 17; i++)
		if (!strcmp(b, nm[i]))
			return i;
	if (!strncmp(b, "xmm", 3) && c_digit((unsigned char)b[3]) && atoi(b + 3) < 16)
		return 17 + atoi(b + 3);
	return -1;
}

static int cfi_reg(As *a, const char **pp, const char *e, uint32_t *r)
{
	const char *p = ws(*pp, e), *s = p;
	if (a->o->machine == EM_X86_64 && p < e && *p == '%') {
		int k;
		s = ++p;
		while (p < e && (isidc((unsigned char)*p) || c_digit((unsigned char)*p))) p++;
		if ((k = cfi_reg86(s, (size_t)(p - s))) < 0) return err(a, "invalid register name");
		*pp = p; *r = (uint32_t)k;
		return 0;
	}
	while (p < e && isidc((unsigned char)*p)) p++;
	size_t n = (size_t)(p - s);
	if (!n) return err(a, "expected register");
	*pp = p;
	if (c_digit((unsigned char)s[0])) {
		uint32_t v = 0;
		for (size_t i = 0; i < n; i++) { if (!c_digit((unsigned char)s[i])) return err(a, "invalid register number"); v = v * 10 + (uint32_t)(s[i] - '0'); }
		*r = v; return 0;
	}
	if ((n == 2 && !strncasecmp(s, "sp", 2)) || (n == 3 && !strncasecmp(s, "wsp", 3))) { *r = 31; return 0; }
	if (n == 2 && !strncasecmp(s, "fp", 2)) { *r = 29; return 0; }
	if (n == 2 && !strncasecmp(s, "lr", 2)) { *r = 30; return 0; }
	char c = (char)(s[0] | 0x20);
	if (n >= 2 && n <= 3 && strchr("xwvbhsdq", c) && c_digit((unsigned char)s[1]) && (n == 2 || c_digit((unsigned char)s[2]))) {
		uint32_t v = (uint32_t)(s[1] - '0');
		if (n == 3) v = v * 10 + (uint32_t)(s[2] - '0');
		if ((c == 'x' || c == 'w') ? v > 30 : v > 31) return err(a, "invalid register name");
		*r = (c == 'x' || c == 'w') ? v : 64 + v;
		return 0;
	}
	return err(a, "invalid register name");
}

static struct Cfi *cfi_push(As *a, uint8_t op)
{
	if (a->ncfi == a->cficap) {
		uint32_t nc = a->cficap ? a->cficap * 2 : 256;
		struct Cfi *t = rget(&a->defs, sizeof *t * nc);
		if (a->ncfi) memcpy(t, a->cfi, sizeof *t * a->ncfi);
		a->cfi = t; a->cficap = nc;
	}
	struct Cfi *c = &a->cfi[a->ncfi++];
	memset(c, 0, sizeof *c);
	c->op = op;
	c->lab = here(a);
	a->fde[a->nfde - 1].n++;
	return c;
}

VPRIV int d_cfi(As *a, Sv w, const char *p, const char *e)
{
#define CIS(lit) (w.n == sizeof(lit) - 1 && !memcmp(w.s, lit, w.n))
	p = ws(p, e);
	if (CIS("cfi_sections")) {
		int eh = 0, dbg = 0;
		while (p < e) {
			const char *s = p;
			while (p < e && *p != ',' && *p != ' ' && *p != '\t') p++;
			Sv t = { s, (uint32_t)(p - s) };
			if (sveq(t, ".eh_frame")) eh = 1;
			else if (sveq(t, ".debug_frame")) dbg = 1;
			p = ws(p, e);
			if (p < e && *p == ',') p = ws(p + 1, e);
		}
		if (dbg) return err(a, ".debug_frame is not supported");
		a->cfi_eh = !eh;
		return 0;
	}
	if (CIS("cfi_startproc")) {
		if (a->infde) return err(a, "starting new .cfi frame before finishing the previous one");
		if (a->nfde == a->fdecap) {
			uint32_t nc = a->fdecap ? a->fdecap * 2 : 64;
			struct Fde *t = rget(&a->defs, sizeof *t * nc);
			if (a->nfde) memcpy(t, a->fde, sizeof *t * a->nfde);
			a->fde = t; a->fdecap = nc;
		}
		struct Fde *f = &a->fde[a->nfde++];
		memset(f, 0, sizeof *f);
		Sv t = trim(p, e);
		if (t.n) { if (!sveq(t, "simple")) return err(a, "unexpected token"); f->simple = 1; }
		f->start = here(a); f->first = a->ncfi; f->ra = a->o->machine == EM_X86_64 ? 16 : 30;
		a->infde = 1;
		return 0;
	}
	if (!a->infde) return err(a, "this directive must appear between .cfi_startproc and .cfi_endproc directives");
	struct Fde *f = &a->fde[a->nfde - 1];
	if (CIS("cfi_endproc")) { f->end = here(a); a->infde = 0; return 0; }
	if (CIS("cfi_signal_frame")) { f->sig = 1; return 0; }
	if (CIS("cfi_b_key_frame")) { f->bkey = 1; return 0; }
	if (CIS("cfi_return_column")) return cfi_reg(a, &p, e, &f->ra);
	if (CIS("cfi_remember_state")) { cfi_push(a, CF_REMEMBER); return 0; }
	if (CIS("cfi_restore_state")) { cfi_push(a, CF_RESTORE_STATE); return 0; }
	if (CIS("cfi_negate_ra_state") || CIS("cfi_window_save")) { cfi_push(a, CF_NEGATE_RA); return 0; }
	if (CIS("cfi_escape")) {
		uint8_t *b = rget(&a->defs, (size_t)(e - p) + 1);
		uint32_t n = 0;
		while (p < e) {
			int64_t v;
			if (abs_expr(a, &p, e, &v)) return -1;
			b[n++] = (uint8_t)v;
			p = ws(p, e);
			if (p < e && *p == ',') p = ws(p + 1, e);
		}
		struct Cfi *c = cfi_push(a, CF_ESCAPE);
		c->esc = b; c->off = n;
		return 0;
	}
	int64_t off = 0;
	uint32_t r1 = 0, r2 = 0;
	uint8_t op;
	if (CIS("cfi_def_cfa_offset") || CIS("cfi_adjust_cfa_offset")) {
		if (abs_expr(a, &p, e, &off)) return -1;
		op = CIS("cfi_adjust_cfa_offset") ? CF_ADJUST : CF_DEF_CFA_OFFSET;
	} else if (CIS("cfi_def_cfa_register") || CIS("cfi_restore") || CIS("cfi_undefined") || CIS("cfi_same_value")) {
		if (cfi_reg(a, &p, e, &r1)) return -1;
		op = CIS("cfi_def_cfa_register") ? CF_DEF_CFA_REGISTER : CIS("cfi_restore") ? CF_RESTORE :
		     CIS("cfi_undefined") ? CF_UNDEFINED : CF_SAME;
	} else if (CIS("cfi_def_cfa") || CIS("cfi_offset") || CIS("cfi_rel_offset") || CIS("cfi_register")) {
		if (cfi_reg(a, &p, e, &r1)) return -1;
		p = ws(p, e);
		if (p >= e || *p != ',') return err(a, "expected comma");
		p++;
		if (CIS("cfi_register")) { if (cfi_reg(a, &p, e, &r2)) return -1; op = CF_REGISTER; }
		else {
			if (abs_expr(a, &p, e, &off)) return -1;
			op = CIS("cfi_def_cfa") ? CF_DEF_CFA : CIS("cfi_offset") ? CF_OFFSET : CF_REL_OFFSET;
		}
	} else return err(a, "unknown directive '.%.*s'", (int)w.n, w.s);
	struct Cfi *c = cfi_push(a, op);
	c->reg = r1; c->reg2 = r2; c->off = off;
	return 0;
#undef CFIS
}



/* .eh_frame as MC's FrameEmitterImpl writes it: per FDE, its CIE first when
 * it is new; entries padded to 4 with DW_CFA_nop */
VPRIV void cfi_finish(As *a)
{
	ElfObj *o = a->o;
	if (a->infde) { err(a, "unfinished frame"); return; }
	if (!a->nfde || a->cfi_eh) return;
	int x86 = o->machine == EM_X86_64;
	uint32_t sec = elf_section(o, ".eh_frame", 9, x86 ? 0x70000001u /* SHT_X86_64_UNWIND */ : SHT_PROGBITS, SHF_ALLOC, 0, 0);
	uint32_t fr = sec;
	if (elf_sec(o, sec)->align < 3) elf_sec(o, sec)->align = 3;
	elf_data(o, fr);
	frag_bases(o);
	struct { uint32_t key, off; } cies[8]; uint32_t ncie = 0;
	size_t cap = 64;
	for (uint32_t i = 0; i < a->nfde; i++) cap += 64 + 16 * (size_t)a->fde[i].n;
	for (uint32_t i = 0; i < a->ncfi; i++) if (a->cfi[i].op == CF_ESCAPE) cap += (size_t)a->cfi[i].off;
	Buf8 b = { rget(&a->defs, cap), 0 };
	size_t *pcbs = rget(&a->defs, sizeof(size_t) * a->nfde);
	/* MC stable-sorts the FDEs by CIE key (signal frame, simple, RA column, B key) */
	uint32_t *ord = rget(&a->defs, sizeof(uint32_t) * a->nfde);
	for (uint32_t i = 0; i < a->nfde; i++) {
		struct Fde *f = &a->fde[i];
		uint32_t key = (uint32_t)f->sig << 30 | (uint32_t)f->simple << 29 | f->ra << 1 | f->bkey;
		uint32_t k = i;
		while (k > 0) {
			struct Fde *g = &a->fde[ord[k - 1]];
			uint32_t gk = (uint32_t)g->sig << 30 | (uint32_t)g->simple << 29 | g->ra << 1 | g->bkey;
			if (gk <= key) break;
			ord[k] = ord[k - 1]; k--;
		}
		ord[k] = i;
	}
	for (uint32_t oi = 0; oi < a->nfde; oi++) {
		uint32_t i = ord[oi];
		struct Fde *f = &a->fde[i];
		uint32_t key = (uint32_t)f->sig << 30 | (uint32_t)f->simple << 29 | f->ra << 1 | f->bkey;
		uint32_t ci;
		for (ci = 0; ci < ncie; ci++) if (cies[ci].key == key) break;
		if (ci == ncie) {
			if (ncie == 8) { err(a, "too many distinct CIEs"); return; }
			cies[ncie].key = key; cies[ncie].off = (uint32_t)b.n; ncie++;
			size_t st = b.n;
			b.n += 4;
			memset(b.p + b.n, 0, 4); b.n += 4;	/* CIE id */
			b.p[b.n++] = 1;				/* version */
			b.p[b.n++] = 'z'; b.p[b.n++] = 'R';
			if (f->sig) b.p[b.n++] = 'S';
			if (f->bkey) b.p[b.n++] = 'B';
			b.p[b.n++] = 0;
			uleb(&b, 1); sleb(&b, x86 ? -8 : -4);
			b.p[b.n++] = (uint8_t)f->ra;
			uleb(&b, 1); b.p[b.n++] = 0x1b;	/* augmentation: pcrel sdata4 */
			if (!f->simple && x86) {	/* cfa = rsp + 8, the return address at cfa - 8 */
				b.p[b.n++] = 0x0c; uleb(&b, 7); uleb(&b, 8);
				b.p[b.n++] = 0x80 | 16; uleb(&b, 1);
			} else if (!f->simple) { b.p[b.n++] = 0x0c; uleb(&b, 31); uleb(&b, 0); }
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
		uleb(&b, 0);
		int64_t cfa = 0;
		uint32_t base = f->start;
		for (uint32_t k = f->first; k < f->first + f->n; k++) {
			struct Cfi *c = &a->cfi[k];
			ElfSym *x = elf_symp(o, base), *y = elf_symp(o, c->lab);
			uint64_t d;
			if (x->frag == y->frag) d = y->value - x->value;
			else if (elf_frag(o, x->frag)->sec == elf_frag(o, y->frag)->sec)	/* split by .prefalign/alignment */
				d = elf_frag(o, y->frag)->base + y->value - elf_frag(o, x->frag)->base - x->value;
			else { err(a, "CFI across sections is not supported"); return; }
			if (d) {
				if (d < 0x40) b.p[b.n++] = (uint8_t)(0x40 | d);
				else if (d <= 0xff) { b.p[b.n++] = 2; b.p[b.n++] = (uint8_t)d; }
				else if (d <= 0xffff) { b.p[b.n++] = 3; b.p[b.n++] = (uint8_t)d; b.p[b.n++] = (uint8_t)(d >> 8); }
				else { b.p[b.n++] = 4; uint32_t d4 = (uint32_t)d; memcpy(b.p + b.n, &d4, 4); b.n += 4; }
				base = c->lab;
			}
			switch (c->op) {
			case CF_DEF_CFA: b.p[b.n++] = 0x0c; uleb(&b, c->reg); cfa = c->off; uleb(&b, (uint64_t)cfa); break;
			case CF_DEF_CFA_OFFSET: case CF_ADJUST:
				cfa = c->op == CF_ADJUST ? cfa + c->off : c->off;
				b.p[b.n++] = 0x0e; uleb(&b, (uint64_t)cfa); break;
			case CF_DEF_CFA_REGISTER: b.p[b.n++] = 0x0d; uleb(&b, c->reg); break;
			case CF_OFFSET: case CF_REL_OFFSET: {
				int64_t v = c->off;
				if (c->op == CF_REL_OFFSET) v -= cfa;
				v /= x86 ? -8 : -4;
				if (v < 0) { b.p[b.n++] = 0x11; uleb(&b, c->reg); sleb(&b, v); }
				else if (c->reg < 64) { b.p[b.n++] = (uint8_t)(0x80 | c->reg); uleb(&b, (uint64_t)v); }
				else { b.p[b.n++] = 0x05; uleb(&b, c->reg); uleb(&b, (uint64_t)v); }
				break;
			}
			case CF_RESTORE:
				if (c->reg < 64) b.p[b.n++] = (uint8_t)(0xc0 | c->reg);
				else { b.p[b.n++] = 0x06; uleb(&b, c->reg); }
				break;
			case CF_UNDEFINED: b.p[b.n++] = 0x07; uleb(&b, c->reg); break;
			case CF_SAME: b.p[b.n++] = 0x08; uleb(&b, c->reg); break;
			case CF_REGISTER: b.p[b.n++] = 0x09; uleb(&b, c->reg); uleb(&b, c->reg2); break;
			case CF_REMEMBER: b.p[b.n++] = 0x0a; break;
			case CF_RESTORE_STATE: b.p[b.n++] = 0x0b; break;
			case CF_NEGATE_RA: b.p[b.n++] = 0x2d; break;
			case CF_ESCAPE: memcpy(b.p + b.n, c->esc, (size_t)c->off); b.n += (size_t)c->off; break;
			}
		}
		while ((b.n - st) & 3) b.p[b.n++] = 0;
		if (oi == a->nfde - 1) while (b.n & 7) b.p[b.n++] = 0;	/* the section ends 8-aligned (MC) */
		uint32_t len = (uint32_t)(b.n - st - 4);
		memcpy(b.p + st, &len, 4);
		pcbs[i] = pcb;
	}
	uint64_t at = elf_frag(o, fr)->size;
	uint8_t *q = elf_grow(o, fr, b.n);
	memcpy(q, b.p, b.n);
	for (uint32_t oi = 0; oi < a->nfde; oi++) {
		uint32_t i = ord[oi];
		struct Fde *f = &a->fde[i];
		elf_fixup(o, fr, at + pcbs[i], q + pcbs[i], x86 ? R_X86_64_PC32 : R_AARCH64_PREL32, (Val){ f->start, 0, 0 });
		elf_fixup(o, fr, at + pcbs[i] + 4, q + pcbs[i] + 4, x86 ? R_X86_64_32 : R_AARCH64_ABS32, (Val){ f->end, f->start, 0 });
	}
}
