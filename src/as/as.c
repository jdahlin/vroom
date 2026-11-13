/* as.c: the statement loop of the assembler (GNU as syntax, LLVM MC's semantics): a stack of frames
 * (the input, macro and .rept expansions), the statement scanner, directive dispatch, the
 * instruction hand-off, and the lifecycle (as_new, as_begin, as_snippet, as_end).
 * Everything else is in the files beside it, one job each; as_SRC in the Makefile lists them. */
#include "asimpl.h"
#include <ctype.h>
#include <stdbool.h>

VPRIV Sv svdup(As *a, const char *s, size_t n)
{
	char *p = rget(&a->defs, n + 1);
	memcpy(p, s, n); p[n] = 0;
	return (Sv){ p, (uint32_t)n };
}

VPRIV uint32_t curline(As *a)
{
	for (int i = a->nfr - 1; i >= 0; i--) if (a->fr[i].kind == FR_FILE) return a->fr[i].sline;
	return a->eofline;
}

VPRIV int err(As *a, const char *fmt, ...)
{
	if (a->err) return -1;
	a->err = 1;
	char *m = a->msg + a->mlen;
	size_t sz = sizeof a->msg - a->mlen;
	/* inside a macro or .rept: its body's line, as llvm-mc's <instantiation> buffer */
	bool inst = a->nfr && a->fr[a->nfr - 1].kind != FR_FILE;
	int k = snprintf(m, sz, "%s%s:%u: error: ", a->mlen ? "\n" : "", inst ? "<instantiation>" : a->fname,
			 inst ? a->fr[a->nfr - 1].sline : curline(a));
	va_list ap; va_start(ap, fmt);
	if ((size_t)k < sz) vsnprintf(m + k, sz - k, fmt, ap);
	va_end(ap);
	return -1;
}

/* ---------------------------------------------------------------- frames and statements */

VPRIV int push_frame(As *a, const char *p, size_t n, int kind, size_t mark)
{
	if (a->nfr >= MAXFR) return err(a, "macros nested too deeply");
	Frame *f = &a->fr[a->nfr++];
	f->p = p; f->e = p + n; f->kind = (uint8_t)kind; f->bol = 1;
	f->ncond = (uint32_t)a->ncs; f->mark = mark; f->line = f->sline = 1;
	f->pline = a->nfr > 1 ? a->fr[a->nfr - 2].sline : 0;
	return 0;
}

VPRIV void pop_frame(As *a)
{
	Frame *f = &a->fr[--a->nfr];
	if (f->kind != FR_FILE) a->xs.used = f->mark;
	else a->eofline = f->line;
}

/* bytes next_stmt must look at: newline, ';', comments, strings */
static const uint8_t sstop[256] = { ['\n'] = 1, [';'] = 1, ['/'] = 1, ['"'] = 1 };

/* first byte of [p, e) in sstop (or e) */
static inline const char *find_stop(const char *p, const char *e)
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
	while (p < e && !sstop[(uint8_t)*p]) p++;
	return p;
}

/* x86-64 text: '#' starts a comment too */
static inline const char *find_stop86(const char *p, const char *e)
{
	while (p < e && !sstop[(uint8_t)*p] && *p != '#') p++;
	return p;
}

/* The general case: copy the statement into a->sb without comments. */
static __attribute__((noinline)) size_t next_stmt_copy(As *a, Frame *f)
{
	const char *p = f->p, *e = f->e;
	char *o = (char *)a->sb.base;
	size_t n = 0, cap = a->sb.cap - 1;
	for (;;) {
		if (p >= e) break;
		char c = *p;
		if (c == '\n') { p++; f->line++; f->bol = 1; break; }
		if (f->bol) {
			if (c == ' ' || c == '\t' || c == '\r') { p++; if (n < cap) o[n++] = c; continue; }
			f->bol = 0;
			if (c == '#') {		/* cpp line marker (# N "file") / line comment */
				const char *q = p + 1;
				while (q < e && (*q == ' ' || *q == '\t')) q++;
				if (f->kind == FR_FILE && q < e && c_digit((unsigned char)*q)) {
					uint32_t ln = 0;
					while (q < e && c_digit((unsigned char)*q)) ln = ln * 10 + (uint32_t)(*q++ - '0');
					while (q < e && (*q == ' ' || *q == '\t')) q++;
					if (q < e && *q == '"') {
						const char *r = ++q;
						while (q < e && *q != '"' && *q != '\n') q++;
						a->fname = svdup(a, r, (size_t)(q - r)).s;
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
		if (!sstop[(uint8_t)c]) {	/* plain run: copy it whole */
			const char *q = a->o->machine == EM_X86_64 ? find_stop86(p + 1, e) : find_stop(p + 1, e);
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
VPRIV __attribute__((always_inline)) size_t next_stmt(As *a, Frame *f, const char **raw, const char **s)
{
	const char *p = f->p, *e = f->e;
	*raw = p;
	f->sline = f->line;
	const char *q = p;
	int bol = f->bol;
	if (a->o->machine == EM_X86_64) goto slow;	/* ('#' comments: the copying scanner) */
	if (bol) {
		while (q < e && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
		if (q < e && *q != '\n') bol = 0;
		if (q < e && *q == '#') goto slow;
	}
	q = find_stop(q, e);
	if (q < e) {
		if (*q == '\n') { f->line++; bol = 1; }
		else if (*q != ';') goto slow;
		f->p = q + 1;
	} else f->p = q;
	f->bol = (uint8_t)bol;
	*s = p;
	return (size_t)(q - p);
slow:
	*s = (const char *)a->sb.base;
	return next_stmt_copy(a, f);
}

/* ---------------------------------------------------------------- statements */

static int define_label(As *a, const char *s, size_t n)
{
	if (c_digit((unsigned char)s[0])) {
		uint32_t num = 0;
		for (size_t i = 0; i < n; i++) num = num * 10 + (uint32_t)(s[i] - '0');
		NumLab *nl = numlab(a, num);
		uint32_t y = nl->fwd ? nl->fwd : tmpsym(a);
		nl->fwd = 0;
		nl->cur = y;
		elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
		return 0;
	}
	uint32_t y = sym(a, s, n);
	ElfSym *ys = elf_symp(a->o, y);
	if (ys->flags & ESYM_DEFINED) return err(a, "invalid symbol redefinition");
	elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
	return 0;
}

/* name = expr / .set name, expr */
static int assign(As *a, Sv n, const char *p, const char *e)
{
	if (n.n == 1 && n.s[0] == '.')	/* . = expr: .org expr */
		return d_org(a, p, e);
	uint32_t y = sym(a, n.s, n.n);
	Val v;
	if (a->o->machine == EM_X86_64) {
		/* name = %reg: the register by another name (crypto code), not a symbol */
		const char *q = ws(p, e), *r;
		const char *t = e;
		while (t > q && (t[-1] == ' ' || t[-1] == '\t')) t--;
		const char *ar = NULL;
		if (q < t && isids((unsigned char)*q) && a->nralias) {	/* X_ = X0: an alias of an alias */
			for (r = q; r < t && isidc((unsigned char)*r); r++)
				;
			if (r == t) ar = as_ralias(a, elf_sym_lookup(a->o, q, (uint32_t)(t - q)));
		}
		if (ar || (q < t && *q == '%')) {
			if (!ar)
				for (r = q + 1; r < t && (isidc((unsigned char)*r) || c_digit((unsigned char)*r)); r++)
					;
			else
				q = ar - 1, r = ar + strlen(ar);
			if (r == (ar ? r : t) && r - q - 1 < 8) {
				if (a->nralias == a->raliascap) {
					uint32_t nc = a->raliascap ? a->raliascap * 2 : 32;
					void *nb = rget(&a->defs, sizeof *a->ralias * nc);
					if (a->nralias) memcpy(nb, a->ralias, sizeof *a->ralias * a->nralias);
					a->ralias = nb, a->raliascap = nc;
				}
				a->ralias[a->nralias].sym = y;
				memset(a->ralias[a->nralias].reg, 0, 8);
				memcpy(a->ralias[a->nralias].reg, q + 1, (size_t)(r - q - 1));
				a->nralias++;
				return 0;
			}
		}
		for (uint32_t i = 0; i < a->nralias; i++)	/* (redefined as a value: no longer an alias) */
			if (a->ralias[i].sym == y) a->ralias[i].reg[0] = 0;
	}
	if (val_expr(a, &p, e, &v)) return -1;
	elf_reg(a->o, y);
	elf_reg(a->o, v.add); elf_reg(a->o, v.sub);
	fold(a, &v);
	ElfSym *s = elf_symp(a->o, y);
	if ((s->flags & ESYM_DEFINED) && !(s->flags & ESYM_ABS) && !(s->flags & ESYM_NOSYMTAB))
		return err(a, "redefinition of '%.*s'", (int)n.n, n.s);
	Var *vr = a->vcount ? var_slot(a, y, 0) : 0;
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
	vr = var_slot(a, y, 1);
	vr->v = v;
	return 0;
}

static int directive(As *a, Sv w, const char *p, const char *e)
{
	const char *d = w.s + 1;
	size_t n = w.n - 1;
#define IS(x) (n == sizeof(x) - 1 && !memcmp(d, x, n))
	if (a->o->machine == EM_X86_64) {
		if (IS("att_syntax") || IS("code64"))
			return 0;	/* (AT&T, 64-bit: what x86-64 text is anyway) */
		if (IS("intel_syntax"))
			return err(a, "Intel syntax is not supported");
	}
	switch (d[0]) {
	case 'a':
		if (IS("altmacro")) { a->altmacro = 1; return 0; }
		if (IS("addrsig")) { a->o->has_addrsig = 1; return 0; }
		if (IS("addrsig_sym")) {
			Sv nm;
			if (parse_name(a, &p, e, &nm)) return -1;
			if (a->nasg == a->asgcap) {
				uint32_t nc = a->asgcap ? a->asgcap * 2 : 64;
				uint32_t *t = rget(&a->defs, sizeof *t * nc);
				if (a->nasg) memcpy(t, a->asg, sizeof *t * a->nasg);
				a->asg = t; a->asgcap = nc;
			}
			a->asg[a->nasg++] = sym(a, nm.s, nm.n);
			return 0;
		}
		if (IS("p2align") || (IS("align") && a->o->machine != EM_X86_64)) return d_align(a, p, e, 1);
		if (IS("align")) return d_align(a, p, e, 0);	/* (x86: a byte count, as .balign) */
		if (IS("ascii")) return d_ascii(a, p, e, 0);
		if (IS("asciz")) return d_ascii(a, p, e, 1);
		if (IS("arch") || IS("arch_extension")) return 0;
		break;
	case 'b':
		if (IS("byte")) return d_data(a, p, e, 1);
		if (IS("balign")) return d_align(a, p, e, 0);
		if (IS("balignw")) return d_align_w(a, p, e, 0, 2);
		if (IS("balignl")) return d_align_w(a, p, e, 0, 4);
		if (IS("base64")) return d_base64(a, p, e);
		if (IS("bss")) { switch_to(a, std_section(a, ".bss")); return 0; }
		break;
	case 'c':
		if (n > 4 && !memcmp(d, "cfi_", 4)) return d_cfi(a, (Sv){ d, (uint32_t)n }, p, e);
		if (IS("comm")) return d_comm(a, p, e, 0);
		if (IS("cpu")) return 0;
		break;
	case 'd':
		if (IS("data")) { switch_to(a, std_section(a, ".data")); return 0; }
		if (IS("double") || IS("dc.d")) return d_float(a, p, e, 8);
		if (IS("dc.s")) return d_float(a, p, e, 4);
		if (IS("dc.b")) return d_data(a, p, e, 1);
		if (IS("dc") || IS("dc.w")) return d_data(a, p, e, 2);
		if (IS("dc.l")) return d_data(a, p, e, 4);
		if (IS("dc.a")) return d_data(a, p, e, 8);
		break;
	case 'e':
		if (IS("equ")) goto set;
		if (IS("end")) { a->ended = 1; return 0; }
		if (IS("error")) {
			Sv t = trim(p, e);
			return err(a, "%.*s", (int)t.n, t.s);
		}
		if (IS("exitm")) return d_exitm(a);
		if (IS("extern")) return 0;
		if (IS("endm") || IS("endmacro")) return err(a, "unexpected '%.*s' in file, no current macro definition", (int)w.n, w.s);
		if (IS("endr")) return err(a, "unexpected '.endr' directive, no current .rept");
		if (IS("equiv")) {
			Sv nm;
			if (parse_name(a, &p, e, &nm)) return -1;
			uint32_t y = elf_sym_lookup(a->o, nm.s, nm.n);
			if (y && (elf_symp(a->o, y)->flags & ESYM_DEFINED)) return err(a, "redefinition of '%.*s'", (int)nm.n, nm.s);
			p = ws(p, e);
			if (p < e && *p == ',') p++;
			return assign(a, nm, p, e);
		}
		break;
	case 'f':
		if (IS("fill")) return d_fill(a, p, e);
		if (IS("float")) return d_float(a, p, e, 4);
		if (IS("file")) {	/* .file "name": an STT_FILE symbol; .file N ...: DWARF */
			p = ws(p, e);
			if (p < e && c_digit((unsigned char)*p)) return d_dwfile(a, p, e);
			if (p < e && *p == '"') {
				const char *q = ++p;
				while (q < e && *q != '"') q++;
				uint32_t y = elf_sym_anon(a->o, svdup(a, p, (size_t)(q - p)).s, (uint32_t)(q - p));
				ElfSym *f = elf_symp(a->o, y);
				f->type = STT_FILE;
				elf_define_abs(a->o, y, 0);
				elf_reg(a->o, y);
			}
			return 0;
		}
		break;
	case 'g':
		if (IS("globl") || IS("global")) return d_symattr(a, p, e, 'g');

		break;
	case 'h':
		if (IS("hword") || IS("half")) return d_data(a, p, e, 2);
		if (IS("hidden")) return d_symattr(a, p, e, 'h');
		break;
	case 'i':
		if (IS("inst")) return d_inst(a, p, e);
		if (IS("int")) return d_data(a, p, e, 4);
		if (IS("incbin")) return d_incbin(a, p, e);
		if (IS("irp")) return d_irp(a, p, e, 0);
		if (IS("irpc")) return d_irp(a, p, e, 1);
		if (IS("internal")) return d_symattr(a, p, e, 'i');
		if (IS("ident")) {
			char buf[4096];
			size_t k;
			if ((size_t)(e - p) > sizeof buf) return err(a, "string too long");
			if (parse_string(a, &p, e, buf, &k)) return -1;
			/* MC's emitIdent: into .comment at once, a NUL before the first */
			uint32_t s = elf_section(a->o, ".comment", 8, SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1, 0);
			elf_data(a->o, s);
			if (!a->nident++) elf_fill(a->o, s, 1, 0);
			elf_bytes(a->o, s, buf, k);
			elf_fill(a->o, s, 1, 0);
			return 0;
		}
		break;
	case 'l':
		if (IS("loc")) return d_loc(a, p, e);
		if (IS("loc_mark_labels")) return 0;
		if (IS("lcomm")) return d_comm(a, p, e, 1);
		if (IS("long")) return d_data(a, p, e, 4);
		if (IS("local")) return d_symattr(a, p, e, 'l');
		if (IS("ltorg") || IS("pool")) return ltorg(a);
		break;
	case 'm':
		if (IS("macro")) return d_macro(a, p, e);
		break;
	case 'n':
		if (IS("noaltmacro")) { a->altmacro = 0; return 0; }
		break;
	case 'o':
		if (IS("org")) return d_org(a, p, e);
		if (IS("octa")) return d_octa(a, p, e);
		break;
	case 'p':
		if (IS("pushsection")) return d_section(a, p, e, 1);
		if (IS("popsection")) {
			if (!a->nss) return err(a, ".popsection without corresponding .pushsection");
			a->nss--;
			if (a->cur != a->ss[a->nss].cur) a->dwlocp = 0;
			a->cur = latest(a, a->ss[a->nss].cur); a->prev = latest(a, a->ss[a->nss].prev);
			return 0;
		}
		if (IS("prefalign")) return d_prefalign(a, p, e);
		if (IS("previous")) { a->prev = latest(a, a->prev); uint32_t t = a->cur; if (a->prev != t) a->dwlocp = 0; a->cur = a->prev; a->prev = t; return 0; }
		if (IS("p2align")) return d_align(a, p, e, 1);
		if (IS("p2alignw")) return d_align_w(a, p, e, 1, 2);
		if (IS("p2alignl")) return d_align_w(a, p, e, 1, 4);
		if (IS("protected")) return d_symattr(a, p, e, 'p');
		if (IS("purgem")) return d_purgem(a, p, e);
		if (IS("print")) {
			char buf[4096];
			size_t k;
			if ((size_t)(e - p) > sizeof buf) return err(a, "string too long");
			if (parse_string(a, &p, e, buf, &k)) return -1;
			printf("%.*s\n", (int)k, buf);	/* llvm-mc: to stdout */
			return 0;
		}
		break;
	case 'q':
		if (IS("quad")) return d_data(a, p, e, 8);
		break;
	case 'r':
		if (IS("rept") || IS("rep")) return d_rept(a, p, e);
		if (IS("reloc")) return d_reloc(a, p, e);
		if (IS("rodata")) { switch_to(a, std_section(a, ".rodata")); return 0; }
		break;
	case 's':
		if (IS("section")) return d_section(a, p, e, 0);
		if (IS("set")) goto set;
		if (IS("size")) return d_size(a, p, e);
		if (IS("symver")) return d_symver(a, p, e);
		if (IS("sleb128")) return d_leb(a, p, e, 1);
		if (IS("short")) return d_data(a, p, e, 2);
		if (IS("single")) return d_float(a, p, e, 4);
		if (IS("space") || IS("skip")) return d_space(a, p, e);
		if (IS("string")) return d_ascii(a, p, e, 1);
		if (IS("subsection")) {
			int64_t sub = 0;
			if (ws(p, e) < e && abs_expr(a, &p, e, &sub)) return -1;
			switch_to(a, elf_subsection(a->o, elf_frag(a->o, a->cur)->sec, (int32_t)sub));
			return 0;
		}
		break;
	case 't':
		if (IS("tlsdesccall")) {	/* R_AARCH64_TLSDESC_CALL on the next instruction */
			Val v;
			if (val_expr(a, &p, e, &v)) return -1;
			elf_fixup(a->o, a->cur, elf_frag(a->o, a->cur)->size, 0, R_AARCH64_TLSDESC_CALL, v);
			if (v.add) elf_symp(a->o, v.add)->type = STT_TLS;
			return 0;
		}
		if (IS("text")) { switch_to(a, std_section(a, ".text")); return 0; }
		if (IS("type")) return d_type(a, p, e);
		break;
	case 'u':
		if (IS("uleb128")) return d_leb(a, p, e, 0);
		if (IS("unreq")) {
			Sv nm = first_word(p, e);
			for (uint32_t i = a->nal; i-- > 0;)
				if (a->al[i].name.n == nm.n && !strncasecmp(a->al[i].name.s, nm.s, nm.n)) {
					a->al[i] = a->al[--a->nal];
					al_rehash(a);
					return 0;
				}
			return 0;
		}
		break;
	case 'v':
		if (IS("variant_pcs")) {	/* STO_AARCH64_VARIANT_PCS */
			Sv nm;
			if (parse_name(a, &p, e, &nm)) return -1;
			uint32_t y = sym(a, nm.s, nm.n);
			elf_symp(a->o, y)->vis |= 0x80;
			elf_reg(a->o, y);
			return 0;
		}
		break;
	case 'w':
		if (IS("weakref")) {	/* .weakref alias, target */
			Sv al, tg;
			if (parse_name(a, &p, e, &al)) return -1;
			p = ws(p, e);
			if (p >= e || *p != ',') return err(a, "expected a comma");
			p++;
			if (parse_name(a, &p, e, &tg)) return -1;
			uint32_t ya = sym(a, al.s, al.n), yt = sym(a, tg.s, tg.n);
			if (a->nsv == a->svcap) {
				uint32_t nc = a->svcap ? a->svcap * 2 : 16;
				void *t = rget(&a->defs, sizeof *a->sv * nc);
				if (a->nsv) memcpy(t, a->sv, sizeof *a->sv * a->nsv);
				a->sv = t; a->svcap = nc;
			}
			a->sv[a->nsv].orig = ya; a->sv[a->nsv].ver = yt; a->sv[a->nsv].rm = 2;
			a->nsv++;
			return 0;
		}
		if (IS("word")) return d_data(a, p, e, a->o->machine == EM_X86_64 ? 2 : 4);	/* (x86: 2 bytes, as GNU as) */
		if (IS("weak")) return d_symattr(a, p, e, 'w');
		if (IS("warning")) {
			Sv t = trim(p, e);
			char buf[4096];
			size_t k = 0;
			const char *q = t.s;
			if (t.n && *q == '"' && (size_t)(e - q) <= sizeof buf && !parse_string(a, &q, e, buf, &k))
				fprintf(stderr, "%s: warning: %.*s\n", a->fname ? a->fname : "<stdin>", (int)k, buf);
			else
				fprintf(stderr, "%s: warning: .warning directive invoked in source file\n", a->fname ? a->fname : "<stdin>");
			return 0;
		}
		break;
	case 'x':
		if (IS("xword")) return d_data(a, p, e, 8);
		break;
	case 'z':
		if (IS("zero")) return d_space(a, p, e);
		break;
	case '2':
		if (IS("2byte")) return d_data(a, p, e, 2);
		break;
	case '4':
		if (IS("4byte")) return d_data(a, p, e, 4);
		break;
	case '8':
		if (IS("8byte")) return d_data(a, p, e, 8);
		break;
	}
	return err(a, "unknown directive '%.*s'", (int)w.n, w.s);
set:;
	Sv nm;
	if (parse_name(a, &p, e, &nm)) return -1;
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected comma");
	return assign(a, nm, p + 1, e);
#undef IS
}

static int statement(As *a, const char *p, const char *e)
{
	p = ws(p, e);
	if (p >= e) return 0;
	Sv w = first_word(p, e);
	int rc, lab = 0;
	if (w.s[0] == '.' && cond_dir(a, w, w.s + w.n, e, &rc)) return rc;
	if (cond_ignore(a)) return 0;
	/* a quoted label: "name": (names with bytes outside ASCII, as clang writes them) */
	while (p < e && *p == '"') {
		const char *t = p;
		Sv qn;

		if (parse_name(a, &t, e, &qn)) return -1;
		t = ws(t, e);
		if (t >= e || *t != ':') break;
		if (define_label(a, qn.s, qn.n)) return -1;
		p = ws(t + 1, e);
		if (p >= e) return 0;
		w = first_word(p, e);
		lab = 1;
	}
	/* labels */
	const char *q;
	for (;;) {
		q = ws(w.s + w.n, e);
		if (!w.n) break;
		if (q < e && *q == ':') {
			int numeric = 1;
			for (uint32_t i = 0; i < w.n; i++) if (!c_digit((unsigned char)w.s[i])) numeric = 0;
			if (!numeric && c_digit((unsigned char)w.s[0])) break;
			if (define_label(a, w.s, w.n)) return -1;
			p = ws(q + 1, e);
			if (p >= e) return 0;
			w = first_word(p, e);
			lab = 1;
			continue;
		}
		break;
	}
	if (!w.n) return err(a, "unexpected token at start of statement");
	if (lab && w.s[0] == '.' && cond_dir(a, w, w.s + w.n, e, &rc)) return rc;	/* "2: .if ..." */
	if (q < e && *q == '=' && !(q + 1 < e && q[1] == '=')) return assign(a, w, q + 1, e);
	if (q + 4 <= e && !memcmp(q, ".req", 4) && (q + 4 == e || !isidc((unsigned char)q[4]))) {
		Sv reg = trim(q + 4, e);
		if (a->nal == a->alcap) {
			uint32_t nc = a->alcap ? a->alcap * 2 : 64;
			Alias *t = rget(&a->defs, sizeof *t * nc);
			if (a->nal) memcpy(t, a->al, sizeof *t * a->nal);
			a->al = t; a->alcap = nc;
		}
		a->al[a->nal].name = svdup(a, w.s, w.n);
		a->al[a->nal].reg = svdup(a, reg.s, reg.n);
		uint32_t h = al_hash(w.s, w.n);
		a->al[a->nal].next = a->alh[h]; a->alh[h] = a->nal + 1;
		a->nal++;
		return 0;
	}
	Macro *m = find_macro(a, w.s, w.n);
	if (m) return invoke(a, m, w.s + w.n, e);
	if (w.s[0] == '.') return directive(a, w, w.s + w.n, e);
	if (a->dwlocp) loc_row(a);
	if (a->o->machine == EM_X86_64)
		return x64_instruction(a, w.s, e, w.n);
	return instruction(a, w.s, e, w.n);
}

/* ---------------------------------------------------------------- driver */

As *as_new(void)
{
	As *a = reserve(sizeof *a);
	a->xs.base = reserve((size_t)1 << 32); a->xs.cap = (size_t)1 << 32;
	a->defs.base = reserve((size_t)1 << 30); a->defs.cap = (size_t)1 << 30;
	a->sb.base = reserve((size_t)1 << 24); a->sb.cap = (size_t)1 << 24;
	return a;
}

void as_stats(const As *a, uint64_t *stmts, uint64_t *bytes) { *stmts = a->nstmt; *bytes = a->nstmtb; }

void as_free(As *a)
{
	munmap(a->xs.base, a->xs.cap); munmap(a->defs.base, a->defs.cap); munmap(a->sb.base, a->sb.cap);
	munmap(a, sizeof *a);
}

void as_set_dir(As *a, const char *dir) { a->dir = dir; }

/*
 * A persistent assembler bound to one ElfObj for a whole TU: as_begin()
 * resets both, as_snippet() runs statements in the current section
 * (macros, .set variables, numeric labels and the section stack persist
 * across snippets, as in one source file), as_end() flushes literal pools
 * and resolves variables.  The text of every snippet must stay valid until
 * as_end() (macro bodies point into it).  as_run() is the three in one.
 */
void as_begin(As *a, ElfObj *o, const char *name)
{
	a->o = o; a->nfr = 0; a->ncs = 0; a->nss = 0; a->err = 0; a->msg[0] = 0; a->mlen = a->nerr = 0;
	a->xs.used = 0; a->defs.used = 0; a->ninst = 0; a->nal = 0; a->alcap = 0; a->al = 0;
	memset(a->alh, 0, sizeof a->alh);
	a->fname = name;
	a->nident = 0; a->ended = 0; a->altmacro = 0; a->eofline = 0;
	a->ncfi = a->cficap = a->nfde = a->fdecap = 0; a->infde = 0; a->cfi_eh = 0; a->nsv = a->svcap = 0; a->nasg = a->asgcap = 0;
	a->ndwf = a->dwfcap = a->ndwd = a->dwdcap = a->ndwl = a->dwlcap = 0; a->npfa = a->pfacap = 0; a->dwlocp = a->dwtab = 0;
	a->var_rank = NULL;
	memset(&a->dwcur, 0, sizeof a->dwcur); a->dwcur.flags = 1;
	a->mmask = 255; a->mcount = 0;
	a->mtab = rget(&a->defs, sizeof(Macro *) * (a->mmask + 1));
	memset(a->mtab, 0, sizeof(Macro *) * (a->mmask + 1));
	a->npool = 0; a->poolcap = 0; a->pool = 0;
	a->vmask = 63; a->vcount = 0;
	a->vars = rget(&a->defs, sizeof(Var) * (a->vmask + 1));
	memset(a->vars, 0, sizeof(Var) * (a->vmask + 1));
	a->nlmask = 255; a->nlcount = 0;
	a->nl = rget(&a->defs, sizeof(NumLab) * (a->nlmask + 1));
	memset(a->nl, 0, sizeof(NumLab) * (a->nlmask + 1));
	elf_reset(o);
	uint32_t text_sec = std_section(a, ".text");
	elf_sec(o, text_sec)->align = 2;
	a->cur = a->prev = text_sec;
}

int as_snippet(As *a, const char *text, size_t len)
{
	ElfObj *o = a->o;
	if (a->err) return -1;
	push_frame(a, text, len, FR_FILE, 0);
	while (a->nfr && !a->err) {
		Frame *f = &a->fr[a->nfr - 1];
		if (f->p >= f->e) { pop_frame(a); continue; }
		const char *raw, *s;
		size_t n = next_stmt(a, f, &raw, &s);
		a->nstmt++; a->nstmtb += n + 1;
		if (statement(a, s, s + n)) {
			size_t k = strlen(a->msg);
			k += (size_t)snprintf(a->msg + k, sizeof a->msg - k, "\n  in: %.*s", (int)(n > 200 ? 200 : n), s);
			/* the instantiations it is in, innermost first (llvm-mc's notes) */
			for (int i = a->nfr - 1; i > 0 && a->fr[i].kind != FR_FILE && k < sizeof a->msg; i--)
				k += (size_t)snprintf(a->msg + k, sizeof a->msg - k, "\n%s:%u: note: while in macro instantiation",
						      a->fr[i - 1].kind == FR_FILE ? a->fname : "<instantiation>", a->fr[i].pline);
			/* as llvm-mc: report it and go on with the next statement (at most 20) */
			a->mlen = (uint32_t)strlen(a->msg);
			if (++a->nerr >= 20 || a->mlen + 600 > sizeof a->msg) break;
			a->err = 0;
			continue;
		}
		if (o->has_err) { err(a, "%s", o->err); break; }
		if (a->ended) break;	/* .end: the rest of the input is ignored */
	}
	a->nfr = 0;
	if (a->nerr) a->err = 1;
	return a->err ? -1 : 0;
}

int as_end(As *a)
{
	ElfObj *o = a->o;
	if (a->ncs && (!a->err || a->nerr)) {	/* (after statement errors too, as llvm-mc) */
		a->err = 0;
		err(a, "unmatched .ifs or .elses");
	}
	if (!a->err && a->npool) flush_pools(a);
	if (!a->err) {
		/* undefined numeric forward references */
		for (uint32_t i = 0; i <= a->nlmask; i++)
			if (a->nl[i].fwd && !(elf_symp(o, a->nl[i].fwd)->flags & ESYM_DEFINED)) { err(a, "directional label undefined"); break; }
	}
	if (!a->err && a->vcount) {
		for (uint32_t i = 0; i <= a->vmask; i++) {
			Var *vr = &a->vars[i];
			if (!vr->sym || vr->v.add == (uint32_t)-1) continue;
			ElfSym *s = elf_symp(o, vr->sym);
			if (s->flags & ESYM_TEMP) continue;
			Val v = vr->v;
			fold(a, &v);
			if (v.add && !v.sub && (elf_symp(o, v.add)->flags & ESYM_DEFINED)) {
				ElfSym *t = elf_symp(o, v.add);
				elf_define(o, vr->sym, t->frag, t->value + (uint64_t)v.c);
				s->alias = t->alias ? t->alias : v.add;
			} else if (!v.add && !v.sub) elf_define_abs(o, vr->sym, (uint64_t)v.c);
			else { err(a, "unsupported variable '%s'", s->name); break; }
		}
	}
	if (!a->err && a->npfa) var_finish(a);
	if (!a->err && a->nfde) cfi_finish(a);
	if (!a->err && a->dwtab) dwline_finish(a);
	if (!a->err && a->nsv) symver_finish(a);
	if (!a->err && a->nasg) {
		o->addrsig = a->asg;	/* in a->defs: lives until the next run, after the object is written */
		o->naddrsig = a->nasg;
	}
	return a->err ? -1 : 0;
}

const char *as_msg(const As *a) { return a->msg; }
uint32_t as_frag(const As *a) { return a->cur; }
int as_failed(const As *a) { return a->err; }

/* a symbol as an operand names it (".L" names are temporary) */
uint32_t as_sym(As *a, const char *name, size_t len) { return sym(a, name, len); }

/* sym + c as an operand expression: .set variables substituted, folded */
ElfVal as_val(As *a, uint32_t y, int64_t c)
{
	Val v = { y, 0, 0 };
	if (a->vcount) {
		Var *vr = var_slot(a, y, 0);
		if (vr && vr->v.add != (uint32_t)-1) v = vr->v;
	}
	fold(a, &v);
	v.c += c;
	fold(a, &v);
	return v;
}

/* "y:" at the current position */
int as_label(As *a, uint32_t y)
{
	ElfSym *ys = elf_symp(a->o, y);
	if (ys->flags & ESYM_DEFINED) return err(a, "invalid symbol redefinition");
	elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
	return 0;
}

int as_run(As *a, ElfObj *o, const char *text, size_t len, const char *name, const char **msg)
{
	as_begin(a, o, name);
	if ((as_snippet(a, text, len) | as_end(a)) != 0) { *msg = a->msg; return -1; }
	return 0;
}
