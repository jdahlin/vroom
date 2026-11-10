/*
 * obj.c: the in-memory object (sections, frags, symbols, fixups; see obj.h)
 * and its layout.  Resolving fixups is per CPU (arch/<cpu>/obj/fixup.c),
 * writing the file per format (platform/linux/elf.c, platform/darwin/macho.c).
 */
#include "objimpl.h"


#define FRAG_CAP (1u << 20)		/* frag ids: sections grow up, subsections down */
#define ARENA_CAP ((size_t)1 << 32)
#define SYMS_CAP ((size_t)256 << 20)
#define FIX_CAP ((size_t)1 << 30)
#define SEC_CAP ((size_t)64 << 20)

_Thread_local jmp_buf *vjob_jb;

_Thread_local int vjob_fail_fd = 2;

void vjob_fail(const char *msg)
{
	dprintf(vjob_fail_fd, "vcc: error: %s\n", msg);
	if (vjob_jb)
		longjmp(*vjob_jb, 1);
	abort();
}

void elf_error(ElfObj *o, const char *fmt, ...)
{
	if (o->has_err) return;
	o->has_err = 1;
	va_list ap; va_start(ap, fmt);
	vsnprintf(o->err, sizeof o->err, fmt, ap);
	va_end(ap);
}

ElfObj *elf_new(void)
{
	ElfObj *o = reserve(sizeof *o);
	o->arena.base = reserve(ARENA_CAP); o->arena.cap = ARENA_CAP;
	o->secs.base = reserve(SEC_CAP); o->secs.cap = SEC_CAP;
	o->frags.base = reserve(FRAG_CAP * sizeof(ElfFrag)); o->frags.cap = FRAG_CAP * sizeof(ElfFrag);
	o->syms.base = reserve(SYMS_CAP); o->syms.cap = SYMS_CAP;
	o->fixes.base = reserve(FIX_CAP); o->fixes.cap = FIX_CAP;
	o->regs.base = reserve(SYMS_CAP); o->regs.cap = SYMS_CAP;
	elf_reset(o);
	return o;
}

void elf_free(ElfObj *o)
{
	munmap(o->arena.base, o->arena.cap); munmap(o->secs.base, o->secs.cap);
	munmap(o->frags.base, o->frags.cap); munmap(o->syms.base, o->syms.cap);
	munmap(o->fixes.base, o->fixes.cap); munmap(o->regs.base, o->regs.cap); munmap(o, sizeof *o);
}

void elf_reset(ElfObj *o)
{
	o->arena.used = 0;
	o->syms.used = sizeof(ElfSym); o->secs.used = sizeof(ElfSec); o->fixes.used = sizeof(ElfFix);
	o->nsec = 1; o->nfrag = FRAG_CAP - 1; o->nsym = 1; o->nfix = 1; o->nreg = 0; o->regs.used = 0;
	o->expr = 0; o->expr_tail = &o->expr;
	memset(o->secs.base, 0, sizeof(ElfSec));
	memset(o->syms.base, 0, sizeof(ElfSym));
	memset(o->fixes.base, 0, sizeof(ElfFix));
	o->hmask = 1023; o->hcount = 0;
	o->htab = rpush(&o->arena, (o->hmask + 1) * 4, 64);
	memset(o->htab, 0, (o->hmask + 1) * 4);
	o->has_err = 0; o->err[0] = 0;
	o->addrsig = 0; o->naddrsig = 0; o->has_addrsig = 0;
}

static const char *copyname(ElfObj *o, const char *s, uint32_t n)
{
	char *p = rpush(&o->arena, n + 1, 1);
	memcpy(p, s, n); p[n] = 0;
	return p;
}

/* ---- symbols ---- */

static uint32_t newsym(ElfObj *o, const char *name, uint32_t len, uint32_t h)
{
	ElfSym *y = rpush(&o->syms, sizeof *y, 8);
	memset(y, 0, sizeof *y);
	y->name = copyname(o, name, len); y->len = len; y->hash = h;
	return o->nsym++;
}

static void hgrow(ElfObj *o)
{
	uint32_t nm = o->hmask * 2 + 1;
	uint32_t *t = rpush(&o->arena, (nm + 1) * 4, 64);
	memset(t, 0, (nm + 1) * 4);
	for (uint32_t i = 0; i <= o->hmask; i++) {
		uint32_t s = o->htab[i];
		if (!s) continue;
		uint32_t j = elf_symp(o, s)->hash & nm;
		while (t[j]) j = (j + 1) & nm;
		t[j] = s;
	}
	o->htab = t; o->hmask = nm;
}

uint32_t elf_sym_lookup(ElfObj *o, const char *name, uint32_t len)
{
	uint32_t h = vhash_obj(name, len);
	for (uint32_t i = h & o->hmask;; i = (i + 1) & o->hmask) {
		uint32_t s = o->htab[i];
		if (!s) return 0;
		ElfSym *y = elf_symp(o, s);
		if (y->hash == h && y->len == len && !memcmp(y->name, name, len)) return s;
	}
}

uint32_t elf_sym(ElfObj *o, const char *name, uint32_t len)
{
	uint32_t h = vhash_obj(name, len), i;
	for (i = h & o->hmask;; i = (i + 1) & o->hmask) {
		uint32_t s = o->htab[i];
		if (!s) break;
		ElfSym *y = elf_symp(o, s);
		if (y->hash == h && y->len == len && !memcmp(y->name, name, len)) return s;
	}
	uint32_t s = newsym(o, name, len, h);
	o->htab[i] = s;
	if (++o->hcount * 2 > o->hmask) hgrow(o);
	return s;
}

uint32_t elf_sym_anon(ElfObj *o, const char *name, uint32_t len) { return newsym(o, name, len, 0); }

void elf_reg_slow(ElfObj *o, uint32_t s)
{
	elf_symp(o, s)->reg = 1;
	*(uint32_t *)rpush(&o->regs, 4, 4) = s;
	o->nreg++;
}

void elf_define(ElfObj *o, uint32_t s, uint32_t frag, uint64_t off)
{
	elf_reg(o, s);
	ElfSym *y = elf_symp(o, s);
	if (y->flags & ESYM_DEFINED) { elf_error(o, "symbol '%s' is already defined", y->name); return; }
	y->flags |= ESYM_DEFINED; y->frag = frag; y->value = off;
}

void elf_define_abs(ElfObj *o, uint32_t s, uint64_t v)
{
	ElfSym *y = elf_symp(o, s);
	y->flags |= ESYM_DEFINED | ESYM_ABS; y->frag = 0; y->value = v;
}

typedef struct ElfExpr { uint32_t sym; int op; ElfVal x, y; struct ElfExpr *next; } ElfExpr;

void elf_expr_sym(ElfObj *o, uint32_t s, int op, ElfVal x, ElfVal y)
{
	ElfExpr *e = rpush(&o->arena, sizeof *e, 8);

	*e = (ElfExpr){ s, op, x, y, 0 };
	if (!o->expr_tail) o->expr_tail = &o->expr;
	*o->expr_tail = e;
	o->expr_tail = &e->next;
}

void elf_size_expr(ElfObj *o, uint32_t s, ElfVal v)
{
	ElfSym *y = elf_symp(o, s);
	y->flags |= ESYM_SIZE_EXPR; y->size_add = v.add; y->size_sub = v.sub; y->size_c = v.c;
}

/* ---- sections and frags ---- */

static uint32_t newfrag(ElfObj *o, uint32_t id, uint32_t sec, int32_t sub)
{
	ElfFrag *f = elf_frag(o, id);
	memset(f, 0, sizeof *f);
	f->sec = sec; f->sub = sub;
	return id;
}

uint32_t elf_find_section(ElfObj *o, const char *name, uint32_t len, uint32_t group)
{
	for (uint32_t i = 1; i < o->nsec; i++) {
		ElfSec *s = elf_sec(o, i);
		if (s->len == len && s->group == group && !memcmp(s->name, name, len)) return i;
	}
	return 0;
}

uint32_t elf_section(ElfObj *o, const char *name, uint32_t len, uint32_t type,
		     uint64_t flags, uint32_t entsize, uint32_t group)
{
	uint32_t i = elf_find_section(o, name, len, group);
	if (i) return i;
	i = o->nsec++;
	ElfSec *s = rpush(&o->secs, sizeof *s, 8);
	(void)s; s = elf_sec(o, i);
	memset(s, 0, sizeof *s);
	s->name = copyname(o, name, len); s->len = len;
	s->type = type; s->flags = flags; s->entsize = entsize; s->group = group;
	if (o->nsec >= o->nfrag)
		vjob_fail("too many sections in the object");
	newfrag(o, i, i, 0);
	/* MC registers the section symbol under the section's name unless a
	 * defined symbol has it; an undefined one becomes the section symbol */
	uint32_t y = elf_sym_lookup(o, name, len);
	if (!y) y = elf_sym(o, name, len);
	else if (elf_symp(o, y)->flags & ESYM_DEFINED) y = elf_sym_anon(o, s->name, len);
	ElfSym *ys = elf_symp(o, y);
	ys->flags = (uint8_t)(ESYM_DEFINED | ESYM_SECTION | (ys->flags & ESYM_USED));
	ys->type = STT_SECTION; ys->frag = i; ys->value = 0; ys->bind = STB_LOCAL;
	elf_reg(o, y);
	s->sym = y;
	return i;
}

uint32_t elf_subsection(ElfObj *o, uint32_t sec, int32_t sub)
{
	if (sub == 0) return sec;
	uint32_t prev = sec, f = elf_frag(o, sec)->next;
	while (f && elf_frag(o, f)->sub < sub) { prev = f; f = elf_frag(o, f)->next; }
	if (f && elf_frag(o, f)->sub == sub) return f;
	if (sub < 0) { elf_error(o, "negative subsection"); return sec; }
	uint32_t n = newfrag(o, o->nfrag--, sec, sub);
	elf_frag(o, n)->next = f;
	elf_frag(o, prev)->next = n;
	return n;
}

uint32_t elf_split(ElfObj *o, uint32_t frag)
{
	if (o->nsec >= o->nfrag) vjob_fail("too many fragments in the object");
	ElfFrag *f = elf_frag(o, frag);
	uint32_t n = newfrag(o, o->nfrag--, f->sec, f->sub);
	ElfFrag *nf = elf_frag(o, n);
	f = elf_frag(o, frag);
	nf->next = f->next; f->next = n; f->cont = n; nf->split = 1;
	return n;
}

/* ---- emission ---- */

uint8_t *elf_grow_slow(ElfObj *o, uint32_t frag, size_t n)
{
	ElfFrag *f = elf_frag(o, frag);
	if (elf_sec(o, f->sec)->type == SHT_NOBITS) {
		elf_error(o, "cannot emit data into NOBITS section '%s'", elf_sec(o, f->sec)->name);
		static uint8_t sink[1 << 16];
		return n <= sizeof sink ? sink : (uint8_t *)rpush(&o->arena, n, 8);
	}
	size_t cap = f->tail ? (size_t)f->tail->cap * 2 : 4096 - sizeof(ElfChunk);
	if (cap > (1u << 20)) cap = 1u << 20;
	if (cap < n) cap = n;
	ElfChunk *c = rpush(&o->arena, sizeof *c + cap, 16);
	c->next = 0; c->used = (uint32_t)n; c->cap = (uint32_t)cap;
	if (f->tail) f->tail->next = c; else f->head = c;
	f->tail = c;
	f->size += n;
	return c->data;
}

void elf_bytes(ElfObj *o, uint32_t frag, const void *p, size_t n)
{
	while (n) {
		size_t k = n > (1u << 20) ? (1u << 20) : n;
		memcpy(elf_grow(o, frag, k), p, k);
		p = (const uint8_t *)p + k; n -= k;
	}
}

void elf_fill(ElfObj *o, uint32_t frag, uint64_t n, uint8_t v)
{
	ElfFrag *f = elf_frag(o, frag);
	if (elf_sec(o, f->sec)->type == SHT_NOBITS) {
		if (v) elf_error(o, "non-zero fill in NOBITS section");
		f->size += n;
		return;
	}
	if (n > o->arena.cap - o->arena.used)	/* fail now, not after zeroing the whole arena (.space 512G) */
		vjob_fail("object too large (an output section or table is full)");
	while (n) {
		size_t k = n > (1u << 20) ? (1u << 20) : n;
		memset(elf_grow(o, frag, k), v, k);
		n -= k;
	}
}

void elf_align(ElfObj *o, uint32_t frag, unsigned p2, int code, uint8_t fill, uint64_t max)
{
	ElfFrag *f = elf_frag(o, frag);
	ElfSec *s = elf_sec(o, f->sec);
	if (p2 > s->align) s->align = p2;
	if (f->sub || f->split) f->has_align |= 1u << p2;	/* checked against the base at layout */
	uint64_t a = (uint64_t)1 << p2, pad = (a - (f->size & (a - 1))) & (a - 1);
	if (!pad || (max && pad > max)) return;
	if (s->type == SHT_NOBITS) { f->size += pad; return; }
	uint8_t *p = elf_grow(o, frag, pad);
	if (!code) { memset(p, fill, pad); return; }
	uint64_t z = pad & 3;
	memset(p, 0, z);
	for (p += z, pad -= z; pad; pad -= 4, p += 4) { p[0] = 0x1f; p[1] = 0x20; p[2] = 0x03; p[3] = 0xd5; }
}

static void mapsym(ElfObj *o, uint32_t frag, const char *n)
{
	uint32_t y = elf_sym_anon(o, n, 2);
	elf_define(o, y, frag, elf_frag(o, frag)->size);
}
void elf_code_slow(ElfObj *o, uint32_t frag)
{
	ElfSec *s = elf_sec(o, elf_frag(o, frag)->sec);
	s->map = 1;
	if (s->align < 2) s->align = 2;
	mapsym(o, frag, "$x");
}
void elf_data_slow(ElfObj *o, uint32_t frag)
{
	elf_sec(o, elf_frag(o, frag)->sec)->map = 2;
	mapsym(o, frag, "$d");
}

/* ---- fixups ---- */

void elf_fixup(ElfObj *o, uint32_t frag, uint64_t off, uint8_t *p, uint32_t type, ElfVal v)
{
	ElfFix *x = rpush(&o->fixes, sizeof *x, 8);
	uint32_t id = o->nfix++;
	x->frag = frag; x->next = 0; x->type = type; x->off = off; x->p = p; x->v = v;
	ElfFrag *f = elf_frag(o, frag);
	if (f->fix_tail) ((ElfFix *)o->fixes.base)[f->fix_tail].next = id; else f->fix_head = id;
	f->fix_tail = id;
	elf_reg(o, v.add); elf_reg(o, v.sub);
}

int elf_sym_frag_off(ElfObj *o, uint32_t s, uint32_t *frag, uint64_t *off)
{
	ElfSym *y = elf_symp(o, s);
	if (!(y->flags & ESYM_DEFINED) || (y->flags & ESYM_ABS)) return 0;
	*frag = y->frag; *off = y->value;
	return 1;
}

/* frag bases, section sizes, .size expressions */
void elf_layout(ElfObj *o)
{
	uint32_t ns = o->nsec;
	/* layout */
	for (uint32_t s = 1; s < ns; s++) {
		uint64_t off = 0;
		for (uint32_t f = s; f; f = elf_frag(o, f)->next) {
			ElfFrag *fr = elf_frag(o, f);
			if (fr->has_align) {
				for (unsigned p2 = 0; p2 < 32; p2++)
					if ((fr->has_align >> p2 & 1) && (off & (((uint64_t)1 << p2) - 1)))
						elf_error(o, "unsupported: alignment inside a subsection that starts unaligned");
			}
			fr->base = off; off += fr->size;
		}
		elf_sec(o, s)->size = off;
	}
	/* symbol sizes */
	for (uint32_t i = 1; i < o->nsym; i++) {
		ElfSym *y = elf_symp(o, i);
		if (!(y->flags & ESYM_SIZE_EXPR)) continue;
		int64_t v = y->size_c;
		if (y->size_add) {
			ElfSym *a = elf_symp(o, y->size_add);
			if (a->flags & ESYM_ABS) v += (int64_t)a->value;
			else if (insec(a)) v += (int64_t)symoff(o, a);
			else { elf_error(o, ".size expression for '%s' is not absolute", y->name); continue; }
		}
		if (y->size_sub) {
			ElfSym *b = elf_symp(o, y->size_sub);
			if (b->flags & ESYM_ABS) v -= (int64_t)b->value;
			else if (insec(b)) v -= (int64_t)symoff(o, b);
			else { elf_error(o, ".size expression for '%s' is not absolute", y->name); continue; }
			if (y->size_add && insec(elf_symp(o, y->size_add)) && insec(b) &&
			    symsec(o, elf_symp(o, y->size_add)) != symsec(o, b))
				elf_error(o, ".size expression for '%s' is not absolute", y->name);
		}
		y->size = (uint64_t)v;
	}
	/* symbols defined as expressions over label differences: in creation order (later ones may use earlier ones) */
	for (ElfExpr *e = o->expr; e; e = e->next) {
		int64_t v[2];
		const ElfVal *xv[2] = { &e->x, &e->y };
		int ok = 1;
		for (int k = 0; k < 2 && ok; k++) {
			ElfSym *a = xv[k]->add ? elf_symp(o, xv[k]->add) : 0, *b = xv[k]->sub ? elf_symp(o, xv[k]->sub) : 0;
			v[k] = xv[k]->c;
			if (a) { if (a->flags & ESYM_ABS) v[k] += (int64_t)a->value; else if (insec(a) && b && insec(b) && symsec(o, a) == symsec(o, b)) v[k] += (int64_t)symoff(o, a); else ok = 0; }
			if (b && ok) { if (b->flags & ESYM_ABS) v[k] -= (int64_t)b->value; else if (insec(b) && a) v[k] -= (int64_t)symoff(o, b); else ok = 0; }
		}
		if (!ok) { elf_error(o, "expected absolute expression"); continue; }
		int64_t p = v[0], q = v[1], r = 0;
		switch (e->op) {
		case '*': r = (int64_t)((uint64_t)p * (uint64_t)q); break;
		case '/': if (!q) { elf_error(o, "division by zero"); continue; } r = p / q; break;
		case '%': if (!q) { elf_error(o, "division by zero"); continue; } r = p % q; break;
		case 'L': r = (int64_t)((uint64_t)p << (q & 63)); break;
		case 'R': r = (int64_t)((uint64_t)p >> (q & 63)); break;
		case '&': r = p & q; break;
		case '|': r = p | q; break;
		case '^': r = p ^ q; break;
		case '!': r = p | ~q; break;
		default: elf_error(o, "bad operator in deferred expression"); continue;
		}
		elf_define_abs(o, e->sym, (uint64_t)r);
	}
}
