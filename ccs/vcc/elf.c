/*
 * elf.c: in-memory ELF64 relocatable writer, byte-compatible with LLVM MC.
 * See elf.h for the model.
 *
 * Output layout (as ELFObjectWriter writes it):
 *   ELF header | section data in section order, each aligned |
 *   .symtab (align 8) | .rela.* in section order (align 8) | .strtab |
 *   pad to 8 | section headers
 * Header order: NULL, .strtab, then each section followed by its .rela,
 * then .symtab.  .strtab holds both section and symbol names, tail-merged
 * the way StringTableBuilder does (strings sorted by reversed spelling,
 * descending; a string that ends the previous one shares its bytes).
 */
#include "elf.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define FRAG_CAP (1u << 20)		/* frag ids: sections grow up, subsections down */
#define ARENA_CAP ((size_t)1 << 32)
#define SYMS_CAP ((size_t)256 << 20)
#define FIX_CAP ((size_t)1 << 30)
#define SEC_CAP ((size_t)64 << 20)

static void *reserve(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
	if (p == MAP_FAILED) { perror("vcc: elf: mmap"); abort(); }
	return p;
}

static void *rpush(ElfRegion *r, size_t n, size_t al)
{
	size_t u = (r->used + al - 1) & ~(al - 1);
	if (u + n > r->cap) { fprintf(stderr, "vcc: elf: region full\n"); abort(); }
	r->used = u + n;
	return r->base + u;
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
}

static uint32_t hash(const char *s, uint32_t n)
{
	uint64_t h = 0x9e3779b97f4a7c15ull ^ n, w;
	const char *e = s + n;
	uint32_t n0 = n, r = n & 7;
	while (n >= 8) { memcpy(&w, s, 8); h = (h ^ w) * 0xff51afd7ed558ccdull; h ^= h >> 32; s += 8; n -= 8; }
	/* the tail (n % 8 bytes, little-endian in the low bits, as a
	 * memcpy into a zeroed word would put them) from whole loads */
	if (r && n0 >= 8) { memcpy(&w, e - 8, 8); w >>= 8 * (8 - r); }
	else if (r >= 4) { uint32_t x, y; memcpy(&x, s, 4); memcpy(&y, e - 4, 4); w = x | (uint64_t)y << (8 * (r - 4)); }
	else if (r) w = (uint64_t)(uint8_t)s[0] | (uint64_t)(uint8_t)s[r / 2] << (8 * (r / 2)) | (uint64_t)(uint8_t)e[-1] << (8 * (r - 1));
	else w = 0;
	h = (h ^ w) * 0xc4ceb9fe1a85ec53ull; h ^= h >> 29;
	return (uint32_t)h | 1;
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
	uint32_t h = hash(name, len);
	for (uint32_t i = h & o->hmask;; i = (i + 1) & o->hmask) {
		uint32_t s = o->htab[i];
		if (!s) return 0;
		ElfSym *y = elf_symp(o, s);
		if (y->hash == h && y->len == len && !memcmp(y->name, name, len)) return s;
	}
}

uint32_t elf_sym(ElfObj *o, const char *name, uint32_t len)
{
	uint32_t h = hash(name, len), i;
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
	if (o->nsec >= o->nfrag) { fprintf(stderr, "vcc: elf: too many sections\n"); abort(); }
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
	if (f->sub) f->has_align |= 1u << p2;	/* checked against the base at layout */
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

static inline uint64_t symoff(ElfObj *o, ElfSym *y) { return elf_frag(o, y->frag)->base + y->value; }
static inline uint32_t symsec(ElfObj *o, ElfSym *y) { return elf_frag(o, y->frag)->sec; }
static inline int insec(ElfSym *y) { return (y->flags & (ESYM_DEFINED | ESYM_ABS)) == ESYM_DEFINED; }

static int is_pcrel(uint32_t t)
{
	switch (t) {
	case R_AARCH64_PREL64: case R_AARCH64_PREL32: case R_AARCH64_PREL16:
	case R_AARCH64_LD_PREL_LO19: case R_AARCH64_ADR_PREL_LO21:
	case R_AARCH64_TSTBR14: case R_AARCH64_CONDBR19:
	case R_AARCH64_JUMP26: case R_AARCH64_CALL26:
		return 1;
	}
	return 0;
}

static void put(uint8_t *p, uint64_t v, int n) { for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> 8 * i); }
static uint32_t get32(uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int fits(int64_t v, int bits) { return v >> (bits - 1) == 0 || v >> (bits - 1) == -1; }

/* Apply a resolved value to the field. */
static void patch(ElfObj *o, ElfFix *x, int64_t v)
{
	uint8_t *p = x->p;
	uint32_t w;
	switch (x->type) {
	case R_AARCH64_ABS64: case R_AARCH64_PREL64: put(p, (uint64_t)v, 8); return;
	case R_AARCH64_ABS32: case R_AARCH64_PREL32:
		if (!(v >= INT32_MIN && v <= (int64_t)UINT32_MAX)) elf_error(o, "fixup value out of range");
		put(p, (uint64_t)v, 4); return;
	case R_AARCH64_ABS16: case R_AARCH64_PREL16:
		if (!(v >= INT16_MIN && v <= (int64_t)UINT16_MAX)) elf_error(o, "fixup value out of range");
		put(p, (uint64_t)v, 2); return;
	case R_VCC_ABS8:
		if (!(v >= INT8_MIN && v <= (int64_t)UINT8_MAX)) elf_error(o, "fixup value out of range");
		put(p, (uint64_t)v, 1); return;
	}
	w = get32(p);
	switch (x->type) {
	case R_AARCH64_JUMP26: case R_AARCH64_CALL26:
		if (v & 3 || !fits(v, 28)) elf_error(o, "fixup out of range (branch)");
		w |= (uint32_t)(v >> 2) & 0x3ffffff; break;
	case R_AARCH64_CONDBR19: case R_AARCH64_LD_PREL_LO19:
		if (v & 3 || !fits(v, 21)) elf_error(o, "fixup out of range (19-bit)");
		w |= ((uint32_t)(v >> 2) & 0x7ffff) << 5; break;
	case R_AARCH64_TSTBR14:
		if (v & 3 || !fits(v, 16)) elf_error(o, "fixup out of range (14-bit)");
		w |= ((uint32_t)(v >> 2) & 0x3fff) << 5; break;
	case R_AARCH64_ADR_PREL_LO21:
		if (!fits(v, 21)) elf_error(o, "fixup out of range (adr)");
		w |= ((uint32_t)v & 3) << 29 | ((uint32_t)(v >> 2) & 0x7ffff) << 5; break;
	case R_AARCH64_ADR_PREL_PG_HI21: case R_AARCH64_ADR_PREL_PG_HI21_NC:
		v >>= 12;
		w |= ((uint32_t)v & 3) << 29 | ((uint32_t)(v >> 2) & 0x7ffff) << 5; break;
	case R_AARCH64_ADD_ABS_LO12_NC: case R_AARCH64_LDST8_ABS_LO12_NC:
		w |= ((uint32_t)v & 0xfff) << 10; break;
	case R_AARCH64_LDST16_ABS_LO12_NC: w |= ((uint32_t)v & 0xfff) >> 1 << 10; break;
	case R_AARCH64_LDST32_ABS_LO12_NC: w |= ((uint32_t)v & 0xfff) >> 2 << 10; break;
	case R_AARCH64_LDST64_ABS_LO12_NC: w |= ((uint32_t)v & 0xfff) >> 3 << 10; break;
	case R_AARCH64_LDST128_ABS_LO12_NC: w |= ((uint32_t)v & 0xfff) >> 4 << 10; break;
	case R_AARCH64_MOVW_UABS_G0: case R_AARCH64_MOVW_UABS_G0_NC: w |= ((uint32_t)v & 0xffff) << 5; break;
	case R_AARCH64_MOVW_UABS_G1: case R_AARCH64_MOVW_UABS_G1_NC: w |= ((uint32_t)(v >> 16) & 0xffff) << 5; break;
	case R_AARCH64_MOVW_UABS_G2: case R_AARCH64_MOVW_UABS_G2_NC: w |= ((uint32_t)(v >> 32) & 0xffff) << 5; break;
	case R_AARCH64_MOVW_UABS_G3: w |= ((uint32_t)(v >> 48) & 0xffff) << 5; break;
	case R_AARCH64_MOVW_SABS_G0: case R_AARCH64_MOVW_SABS_G1: case R_AARCH64_MOVW_SABS_G2: {
		/* negative: MOVZ becomes MOVN of the inverted value (as MC's backend does) */
		if (v < 0) { v = ~v; w &= ~(1u << 30); }
		unsigned g = x->type - R_AARCH64_MOVW_SABS_G0;
		w |= ((uint32_t)((uint64_t)v >> 16 * g) & 0xffff) << 5;
		break;
	}
	default: elf_error(o, "cannot resolve fixup type %u", x->type); return;
	}
	put(p, w, 4);
}

/* Relocation records, kept per section in layout order. */
typedef struct { uint64_t off; uint32_t sym, type; int64_t addend; uint32_t sec, pad; } Rel;

typedef struct {
	Rel *rel; uint32_t nrel;
	uint32_t *symtab; uint32_t nsymtab, nlocal;	/* symbol ids in output order */
	uint32_t symtab_idx;
	uint8_t *strtab; uint32_t strsize;
	uint32_t *secname, *relaname; uint32_t symtab_name, strtab_name;
	uint64_t *secoff, *relaoff, symoff_, stroff, shoff, size;
	uint32_t *relbeg;	/* per section: first Rel index */
	uint32_t nshdr;
} Out;

static Out out_of(ElfObj *o) { (void)o; Out z; memset(&z, 0, sizeof z); return z; }

static void reloc(ElfObj *o, Out *w, ElfFix *x, uint64_t P, uint32_t type, ElfSym *a, uint32_t aid, int64_t add)
{
	uint32_t s = elf_frag(o, x->frag)->sec;
	if (a && insec(a) && a->bind == STB_LOCAL && !(a->flags & ESYM_SECTION) && a->type != STT_GNU_IFUNC) {
		add += (int64_t)symoff(o, a);
		aid = elf_sec(o, symsec(o, a))->sym;
		elf_symp(o, aid)->flags |= ESYM_USED;
	} else if (a) {
		if ((a->flags & ESYM_TEMP) && !insec(a)) { elf_error(o, "undefined temporary symbol '%s'", a->name); return; }
		a->flags |= ESYM_USED;
	}
	Rel *r = &w->rel[w->nrel++];
	r->off = P; r->sym = aid; r->type = type; r->addend = add; r->sec = s;
	elf_sec(o, s)->nfix_reloc++;
}

static void resolve(ElfObj *o, Out *w, ElfFix *x)
{
	ElfFrag *f = elf_frag(o, x->frag);
	uint32_t S = f->sec;
	uint64_t P = f->base + x->off;
	ElfSym *a = x->v.add ? elf_symp(o, x->v.add) : 0;
	ElfSym *b = x->v.sub ? elf_symp(o, x->v.sub) : 0;
	int64_t c = x->v.c;
	uint32_t type = x->type;
	if (a && (a->flags & ESYM_ABS)) { c += (int64_t)a->value; a = 0; }
	if (b && (b->flags & ESYM_ABS)) { c -= (int64_t)b->value; b = 0; }
	if (b) {
		if (!insec(b)) { elf_error(o, "symbol '%s' can not be undefined in a subtraction expression", b->name); return; }
		if (a && insec(a) && symsec(o, a) == symsec(o, b)) {
			patch(o, x, (int64_t)symoff(o, a) - (int64_t)symoff(o, b) + c);
			return;
		}
		if (symsec(o, b) != S) { elf_error(o, "cannot represent a difference across sections"); return; }
		c += (int64_t)P - (int64_t)symoff(o, b);
		switch (type) {
		case R_AARCH64_ABS64: type = R_AARCH64_PREL64; break;
		case R_AARCH64_ABS32: type = R_AARCH64_PREL32; break;
		case R_AARCH64_ABS16: type = R_AARCH64_PREL16; break;
		default: elf_error(o, "unsupported pc-relative fixup"); return;
		}
		if (!a) { patch(o, x, c); return; }
		x->type = type;
		reloc(o, w, x, P, type, a, x->v.add, c);
		return;
	}
	if (!a) {
		if (is_pcrel(type) && type != R_AARCH64_PREL64 && type != R_AARCH64_PREL32 && type != R_AARCH64_PREL16) {
			/* branch to an absolute address: MC emits a reloc against nothing */
			reloc(o, w, x, P, type, 0, 0, c);
			return;
		}
		patch(o, x, c);
		return;
	}
	if (is_pcrel(type) && insec(a) && symsec(o, a) == S && a->bind == STB_LOCAL && a->type != STT_GNU_IFUNC) {
		patch(o, x, (int64_t)symoff(o, a) + c - (int64_t)P);
		return;
	}
	reloc(o, w, x, P, type, a, x->v.add, c);
}

/* ---- string table ---- */

typedef struct { const char *s; uint32_t n; uint32_t *out; } Str;

static int strcmp_rev(const void *pa, const void *pb)
{
	const Str *a = pa, *b = pb;
	uint32_t i = 0;
	for (;; i++) {
		int ca = i < a->n ? (unsigned char)a->s[a->n - 1 - i] : -1;
		int cb = i < b->n ? (unsigned char)b->s[b->n - 1 - i] : -1;
		if (ca != cb) return ca > cb ? -1 : 1;	/* descending */
		if (ca < 0) return 0;
	}
}

static void build_strtab(ElfObj *o, Out *w, Str *v, uint32_t n)
{
	qsort(v, n, sizeof *v, strcmp_rev);
	uint32_t size = 1;
	const char *prev = 0; uint32_t prevn = 0, prevoff = 0;
	for (uint32_t i = 0; i < n; i++) {
		if (prev && prevn >= v[i].n && !memcmp(prev + prevn - v[i].n, v[i].s, v[i].n)) {
			*v[i].out = prevoff + prevn - v[i].n;
			continue;
		}
		*v[i].out = size; prevoff = size; prev = v[i].s; prevn = v[i].n;
		size += v[i].n + 1;
	}
	uint8_t *t = rpush(&o->arena, size, 8);
	t[0] = 0;
	prev = 0;
	for (uint32_t i = 0; i < n; i++) {
		memcpy(t + *v[i].out, v[i].s, v[i].n);
		t[*v[i].out + v[i].n] = 0;
	}
	w->strtab = t; w->strsize = size;
}

/* ---- finish ---- */

static uint64_t al(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

size_t elf_finish(ElfObj *o)
{
	Out *w = rpush(&o->arena, sizeof *w, 8);
	*w = out_of(o);
	o->fin = w;
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
	/* fixups, in layout order */
	w->rel = rpush(&o->arena, sizeof(Rel) * (o->nfix + 1), 8);
	ElfFix *fx = (ElfFix *)o->fixes.base;
	for (uint32_t s = 1; s < ns; s++)
		for (uint32_t f = s; f; f = elf_frag(o, f)->next)
			for (uint32_t i = elf_frag(o, f)->fix_head; i; i = fx[i].next)
				resolve(o, w, &fx[i]);
	if (o->has_err) return 0;
	/* relocations are already grouped by section (sections processed in order) */
	w->relbeg = rpush(&o->arena, sizeof(uint32_t) * (ns + 1), 8);
	{
		uint32_t k = 0;
		for (uint32_t s = 1; s < ns; s++) { w->relbeg[s] = k; k += elf_sec(o, s)->nfix_reloc; }
		w->relbeg[ns] = k;
	}
	/* symbol table: locals then globals, creation order */
	w->symtab = rpush(&o->arena, sizeof(uint32_t) * o->nsym * 2, 8);
	uint32_t *glob = w->symtab + o->nsym;
	uint32_t nl = 0, ng = 0;
	uint32_t *rl = (uint32_t *)o->regs.base;
	for (uint32_t r = 0; r < o->nreg; r++) {
		uint32_t i = rl[r];
		ElfSym *y = elf_symp(o, i);
		int in;
		if (y->flags & ESYM_SECTION) in = (y->flags & ESYM_USED) != 0;
		else if (y->flags & ESYM_USED) in = 1;
		else in = !(y->flags & (ESYM_TEMP | ESYM_NOSYMTAB));
		if (!in) continue;
		if (!(y->flags & ESYM_DEFINED) && y->bind == STB_LOCAL) y->bind = STB_GLOBAL;
		if (y->bind == STB_LOCAL) w->symtab[nl++] = i; else glob[ng++] = i;
	}
	memmove(w->symtab + nl, glob, ng * sizeof(uint32_t));
	w->nsymtab = nl + ng; w->nlocal = nl;
	for (uint32_t k = 0; k < w->nsymtab; k++) elf_symp(o, w->symtab[k])->idx = k + 1;
	/* section indices */
	uint32_t idx = 2;
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		se->idx = idx++;
		se->rela_idx = se->nfix_reloc ? idx++ : 0;
	}
	w->symtab_idx = idx++;
	w->nshdr = idx;
	/* strings */
	uint32_t nstr = 0, cap = w->nsymtab + 2 * ns + 2;
	Str *sv = rpush(&o->arena, sizeof(Str) * cap, 8);
	w->secname = rpush(&o->arena, sizeof(uint32_t) * ns * 2, 8);
	w->relaname = w->secname + ns;
	uint32_t *symname = rpush(&o->arena, sizeof(uint32_t) * (w->nsymtab + 1), 8);
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		sv[nstr++] = (Str){ se->name, se->len, &w->secname[s] };
		if (se->rela_idx) {
			char *rn = rpush(&o->arena, se->len + 6, 1);
			memcpy(rn, ".rela", 5); memcpy(rn + 5, se->name, se->len); rn[se->len + 5] = 0;
			sv[nstr++] = (Str){ rn, se->len + 5, &w->relaname[s] };
		}
	}
	sv[nstr++] = (Str){ ".symtab", 7, &w->symtab_name };
	sv[nstr++] = (Str){ ".strtab", 7, &w->strtab_name };
	for (uint32_t k = 0; k < w->nsymtab; k++) {
		ElfSym *y = elf_symp(o, w->symtab[k]);
		if (y->flags & ESYM_SECTION) { symname[k] = 0; continue; }
		if (!y->len) { symname[k] = 0; continue; }
		sv[nstr++] = (Str){ y->name, y->len, &symname[k] };
	}
	build_strtab(o, w, sv, nstr);
	for (uint32_t k = 0; k < w->nsymtab; k++) elf_symp(o, w->symtab[k])->size_sub = symname[k];	/* reuse: name offset */
	/* file offsets */
	w->secoff = rpush(&o->arena, sizeof(uint64_t) * ns * 2, 8);
	w->relaoff = w->secoff + ns;
	uint64_t off = 64;
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		off = al(off, (uint64_t)1 << se->align);
		w->secoff[s] = off;
		if (se->type != SHT_NOBITS) off += se->size;
	}
	off = al(off, 8); w->symoff_ = off; off += 24 * (uint64_t)(w->nsymtab + 1);
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		if (!se->rela_idx) continue;
		off = al(off, 8); w->relaoff[s] = off; off += 24 * (uint64_t)se->nfix_reloc;
	}
	w->stroff = off; off += w->strsize;
	off = al(off, 8); w->shoff = off; off += 64 * (uint64_t)w->nshdr;
	w->size = off;
	return off;
}

static void p16(uint8_t *p, uint64_t v) { put(p, v, 2); }
static void p32(uint8_t *p, uint64_t v) { put(p, v, 4); }
static void p64(uint8_t *p, uint64_t v) { put(p, v, 8); }

static void shdr(uint8_t *p, uint32_t name, uint32_t type, uint64_t flags, uint64_t off, uint64_t size,
		 uint32_t link, uint32_t info, uint64_t align, uint64_t entsize)
{
	p32(p, name); p32(p + 4, type); p64(p + 8, flags); p64(p + 16, 0); p64(p + 24, off);
	p64(p + 32, size); p32(p + 40, link); p32(p + 44, info); p64(p + 48, align); p64(p + 56, entsize);
}

void elf_emit(ElfObj *o, uint8_t *b)
{
	Out *w = o->fin;
	uint32_t ns = o->nsec;
	memset(b, 0, w->size);
	static const uint8_t ident[16] = { 0x7f, 'E', 'L', 'F', 2, 1, 1, 0 };
	memcpy(b, ident, 16);
	p16(b + 16, 1); p16(b + 18, 183); p32(b + 20, 1);
	p64(b + 40, w->shoff); p16(b + 52, 64); p16(b + 58, 64); p16(b + 60, w->nshdr); p16(b + 62, 1);
	/* section data */
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		if (se->type == SHT_NOBITS) continue;
		uint8_t *d = b + w->secoff[s];
		for (uint32_t f = s; f; f = elf_frag(o, f)->next) {
			ElfFrag *fr = elf_frag(o, f);
			uint8_t *q = d + fr->base;
			for (ElfChunk *c = fr->head; c; c = c->next) { memcpy(q, c->data, c->used); q += c->used; }
		}
	}
	/* symtab */
	uint8_t *y = b + w->symoff_ + 24;
	for (uint32_t k = 0; k < w->nsymtab; k++, y += 24) {
		ElfSym *s = elf_symp(o, w->symtab[k]);
		uint16_t shndx = 0; uint64_t val = 0;
		if (s->flags & ESYM_ABS) { shndx = 0xfff1; val = s->value; }
		else if (s->flags & ESYM_DEFINED) { shndx = (uint16_t)elf_sec(o, symsec(o, s))->idx; val = symoff(o, s); }
		uint8_t type = s->type;
		uint64_t size = s->size;
		if (s->alias) {	/* mergeTypeForSet / inherited size */
			ElfSym *b = elf_symp(o, s->alias);
			uint8_t t = b->type;
			switch (type) {
			case STT_GNU_IFUNC: if (t == STT_FUNC || t == STT_OBJECT || t == STT_NOTYPE || t == STT_TLS) t = STT_GNU_IFUNC; break;
			case STT_FUNC: if (t == STT_OBJECT || t == STT_NOTYPE || t == STT_TLS) t = STT_FUNC; break;
			case STT_OBJECT: if (t == STT_NOTYPE) t = STT_OBJECT; break;
			case STT_TLS: if (t == STT_OBJECT || t == STT_NOTYPE || t == STT_GNU_IFUNC || t == STT_FUNC) t = STT_TLS; break;
			}
			type = t;
			if (!size && !(s->flags & ESYM_SIZE_EXPR)) size = b->size;
		}
		p32(y, s->size_sub);
		y[4] = (uint8_t)(s->bind << 4 | type); y[5] = s->vis;
		p16(y + 6, shndx); p64(y + 8, val); p64(y + 16, size);
	}
	/* rela */
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		if (!se->rela_idx) continue;
		uint8_t *r = b + w->relaoff[s];
		for (uint32_t i = w->relbeg[s]; i < w->relbeg[s + 1]; i++, r += 24) {
			Rel *e = &w->rel[i];
			p64(r, e->off);
			p64(r + 8, (uint64_t)(e->sym ? elf_symp(o, e->sym)->idx : 0) << 32 | e->type);
			p64(r + 16, (uint64_t)e->addend);
		}
	}
	memcpy(b + w->stroff, w->strtab, w->strsize);
	/* headers */
	uint8_t *h = b + w->shoff;
	shdr(h + 64, w->strtab_name, SHT_STRTAB, 0, w->stroff, w->strsize, 0, 0, 1, 0);
	for (uint32_t s = 1; s < ns; s++) {
		ElfSec *se = elf_sec(o, s);
		shdr(h + 64 * se->idx, w->secname[s], se->type, se->flags, w->secoff[s], se->size, 0, 0,
		     (uint64_t)1 << se->align, se->entsize);
		if (se->rela_idx)
			shdr(h + 64 * se->rela_idx, w->relaname[s], SHT_RELA, SHF_INFO_LINK, w->relaoff[s],
			     24 * (uint64_t)se->nfix_reloc, w->symtab_idx, se->idx, 8, 24);
	}
	shdr(h + 64 * w->symtab_idx, w->symtab_name, SHT_SYMTAB, 0, w->symoff_, 24 * (uint64_t)(w->nsymtab + 1),
	     1, w->nlocal + 1, 8, 24);
}

int elf_write_file(ElfObj *o, const char *path)
{
	size_t n = elf_finish(o);
	if (!n) return -1;
	uint8_t *b = rpush(&o->arena, n, 16);
	elf_emit(o, b);
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) { elf_error(o, "%s: %s", path, strerror(errno)); return -1; }
	size_t done = 0;
	while (done < n) {
		ssize_t k = write(fd, b + done, n - done);
		if (k <= 0) { elf_error(o, "%s: %s", path, strerror(errno)); close(fd); return -1; }
		done += (size_t)k;
	}
	close(fd);
	return 0;
}
