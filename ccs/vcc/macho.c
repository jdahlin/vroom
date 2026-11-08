/*
 * Mach-O (arm64, MH_OBJECT) writer for an ElfObj: the code generator and
 * the assembler build the same section/frag/symbol/fixup model as for ELF
 * (elf.h); this lays it out as a Mach-O relocatable object for ld64.
 *
 *   sections  ELF names map to a fixed set of Mach-O sections (.text ->
 *             __TEXT,__text, .rodata* -> __TEXT,__const or __DATA,__const
 *             when it holds pointers, .data, .bss, .tdata/.tbss/.tlv ->
 *             the thread-local sections, .init_array -> __mod_init_func);
 *             several ELF sections may share one Mach-O section.
 *             Non-allocated sections are dropped.
 *   symbols   names are written as they are (the code generator adds
 *             the '_'); .L temporaries that a relocation needs become
 *             local 'l' symbols.  Order: locals, external definitions,
 *             undefined (both sorted by name), as LC_DYSYMTAB wants.
 *   fixups    pc-relative ones inside one Mach-O section are resolved in
 *             place (no MH_SUBSECTIONS_VIA_SYMBOLS: a section is one
 *             atom); the rest become ARM64_RELOC_* entries, always
 *             against a symbol, addends in ARM64_RELOC_ADDEND or in place.
 *
 * Errors go to o->err like elf_finish's.
 */
#include <stdlib.h>
#include <string.h>

#include "elf.h"

enum {
	ARM64_RELOC_UNSIGNED, ARM64_RELOC_SUBTRACTOR, ARM64_RELOC_BRANCH26, ARM64_RELOC_PAGE21,
	ARM64_RELOC_PAGEOFF12, ARM64_RELOC_GOT_LOAD_PAGE21, ARM64_RELOC_GOT_LOAD_PAGEOFF12,
	ARM64_RELOC_POINTER_TO_GOT, ARM64_RELOC_TLVP_LOAD_PAGE21, ARM64_RELOC_TLVP_LOAD_PAGEOFF12,
	ARM64_RELOC_ADDEND,
};

enum {
	S_REGULAR = 0, S_ZEROFILL = 1, S_MOD_INIT_FUNC_POINTERS = 9, S_THREAD_LOCAL_REGULAR = 0x11,
	S_THREAD_LOCAL_ZEROFILL = 0x12, S_THREAD_LOCAL_VARIABLES = 0x13,
	S_ATTR_PURE_INSTRUCTIONS = 0x80000000u, S_ATTR_SOME_INSTRUCTIONS = 0x400,
};

enum { N_EXT = 1, N_SECT = 0xe, N_PEXT = 0x10, N_WEAK_REF = 0x40, N_WEAK_DEF = 0x80 };

/* the output sections, in segment order (zero-fill last) */
enum { M_TEXT, M_CONST, M_DCONST, M_DATA, M_TLV, M_TDATA, M_INIT, M_BSS, M_TBSS, M_N };
static const struct { const char *seg, *sect; uint32_t flags; } mtab[M_N] = {
	[M_TEXT] = { "__TEXT", "__text", S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS },
	[M_CONST] = { "__TEXT", "__const", S_REGULAR },
	[M_DCONST] = { "__DATA", "__const", S_REGULAR },
	[M_DATA] = { "__DATA", "__data", S_REGULAR },
	[M_TLV] = { "__DATA", "__thread_vars", S_THREAD_LOCAL_VARIABLES },
	[M_TDATA] = { "__DATA", "__thread_data", S_THREAD_LOCAL_REGULAR },
	[M_INIT] = { "__DATA", "__mod_init_func", S_MOD_INIT_FUNC_POINTERS },
	[M_BSS] = { "__DATA", "__bss", S_ZEROFILL },
	[M_TBSS] = { "__DATA", "__thread_bss", S_THREAD_LOCAL_ZEROFILL },
};

typedef struct { uint32_t addr; uint32_t word; } MRel;	/* r_address, packed info */

typedef struct {
	int8_t *map;		/* per ELF section: M_* or -1 */
	uint64_t *vaddr;	/* per ELF section: address in the object */
	uint64_t maddr[M_N], msize[M_N], moff[M_N];
	uint32_t malign[M_N], midx[M_N];	/* midx: 1-based Mach-O section number, 0 = absent */
	uint32_t nmsec;
	MRel *rel[M_N]; uint32_t nrel[M_N], reloff[M_N];
	uint32_t *nl; uint32_t nnl, nlocal, nextdef, nundef;	/* symbol ids in nlist order */
	uint32_t *nlidx;	/* per symbol: nlist index + 1 */
	char *str; uint32_t strsize; uint32_t *strx;	/* per symbol */
	uint64_t cmdsize, dataoff, symoff, stroff, size;
	uint8_t *needsym;	/* per symbol: a relocation names it */
} Mo;

static void *zalloc(ElfObj *o, size_t n)
{
	/* the object's arena: freed with the object */
	ElfRegion *r = &o->arena;
	size_t at = (r->used + 15) & ~(size_t)15;
	if (at + n > r->cap) { elf_error(o, "out of memory"); return NULL; }
	r->used = at + n;
	memset(r->base + at, 0, n);
	return r->base + at;
}

static int pfx(const ElfSec *se, const char *p)
{
	size_t n = strlen(p);
	return se->len >= n && !memcmp(se->name, p, n) && (se->len == n || se->name[n] == '.');
}

static int is_reloc_word(uint32_t t) { return t == R_AARCH64_ABS64 || t == R_AARCH64_ABS32; }

static int sec_map(ElfObj *o, uint32_t s)
{
	ElfSec *se = elf_sec(o, s);
	if (!(se->flags & SHF_ALLOC)) return -1;
	if (pfx(se, ".text")) return M_TEXT;
	if (pfx(se, ".tbss")) return M_TBSS;
	if (pfx(se, ".tdata")) return M_TDATA;
	if (pfx(se, ".tlv")) return M_TLV;
	if (pfx(se, ".bss")) return M_BSS;
	if (pfx(se, ".init_array")) return M_INIT;
	if (pfx(se, ".data")) return M_DATA;
	if (pfx(se, ".rodata")) {
		/* pointers need rebasing: __DATA,__const */
		ElfFix *fx = (ElfFix *)o->fixes.base;
		for (uint32_t f = s; f; f = elf_frag(o, f)->next)
			for (uint32_t i = elf_frag(o, f)->fix_head; i; i = fx[i].next)
				if (is_reloc_word(fx[i].type) && fx[i].v.add && !(elf_symp(o, fx[i].v.add)->flags & ESYM_ABS) && !fx[i].v.sub)
					return M_DCONST;
		return M_CONST;
	}
	if (se->flags & SHF_EXECINSTR) return M_TEXT;
	if (se->type == SHT_NOBITS) return M_BSS;
	return (se->flags & SHF_WRITE) ? M_DATA : M_CONST;
}

static inline int defined(const ElfSym *y) { return (y->flags & (ESYM_DEFINED | ESYM_ABS)) == ESYM_DEFINED; }

static inline uint64_t sym_addr(ElfObj *o, Mo *m, const ElfSym *y)
{
	ElfFrag *f = elf_frag(o, y->frag);
	return m->vaddr[f->sec] + f->base + y->value;
}

static inline int sym_msec(ElfObj *o, Mo *m, const ElfSym *y) { return m->map[elf_frag(o, y->frag)->sec]; }

static void addrel(ElfObj *o, Mo *m, int ms, uint32_t addr, uint32_t symnum, int pcrel, int len, int ext, int type)
{
	(void)o;
	m->rel[ms][m->nrel[ms]++] = (MRel){ addr, (symnum & 0xffffff) | (uint32_t)pcrel << 24 | (uint32_t)len << 25 |
					   (uint32_t)ext << 27 | (uint32_t)type << 28 };
}

static void symrel(ElfObj *o, Mo *m, int ms, uint32_t addr, uint32_t sid, int pcrel, int len, int type, int64_t add)
{
	ElfSym *y = elf_symp(o, sid);
	if ((y->flags & ESYM_TEMP) && !defined(y)) { elf_error(o, "undefined temporary symbol '%.*s'", (int)y->len, y->name); return; }
	if (add) {
		if (add < -(1 << 23) || add >= (1 << 23)) { elf_error(o, "relocation addend out of range"); return; }
		addrel(o, m, ms, addr, (uint32_t)add, 0, 2, 0, ARM64_RELOC_ADDEND);
	}
	m->needsym[sid] = 1;
	addrel(o, m, ms, addr, sid, pcrel, len, 1, type);	/* symnum: the id, mapped when written */
}

static void put(uint8_t *p, uint64_t v, int n) { for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> 8 * i); }

static void resolve(ElfObj *o, Mo *m, ElfFix *x)
{
	ElfFrag *f = elf_frag(o, x->frag);
	int ms = m->map[f->sec];
	if (ms < 0) return;	/* dropped section */
	uint64_t P = m->vaddr[f->sec] + f->base + x->off;
	uint32_t raddr = (uint32_t)(P - m->maddr[ms]);
	uint32_t aid = x->v.add, bid = x->v.sub;
	ElfSym *a = aid ? elf_symp(o, aid) : 0, *b = bid ? elf_symp(o, bid) : 0;
	int64_t c = x->v.c;
	uint32_t t = x->type;

	if (a && (a->flags & ESYM_ABS)) { c += (int64_t)a->value; a = 0; aid = 0; }
	if (b && (b->flags & ESYM_ABS)) { c -= (int64_t)b->value; b = 0; bid = 0; }
	if (b) {
		if (!defined(b)) { elf_error(o, "symbol '%s' can not be undefined in a subtraction expression", b->name); return; }
		if (a && defined(a) && sym_msec(o, m, a) == sym_msec(o, m, b)) {
			elf_patch(o, x, (int64_t)sym_addr(o, m, a) - (int64_t)sym_addr(o, m, b) + c);
			return;
		}
		if (!a || !is_reloc_word(t)) { elf_error(o, "unsupported difference relocation"); return; }
		/* a - b + c: SUBTRACTOR(b) then UNSIGNED(a), c in place */
		int len = t == R_AARCH64_ABS64 ? 3 : 2;
		m->needsym[bid] = m->needsym[aid] = 1;
		addrel(o, m, ms, raddr, bid, 0, len, 1, ARM64_RELOC_SUBTRACTOR);
		addrel(o, m, ms, raddr, aid, 0, len, 1, ARM64_RELOC_UNSIGNED);
		put(x->p, (uint64_t)c, len == 3 ? 8 : 4);
		return;
	}
	if (!a) {
		switch (t) {
		case R_AARCH64_ABS64: case R_AARCH64_ABS32: case R_AARCH64_ABS16: case R_VCC_ABS8:
		case R_AARCH64_MOVW_UABS_G0: case R_AARCH64_MOVW_UABS_G0_NC: case R_AARCH64_MOVW_UABS_G1:
		case R_AARCH64_MOVW_UABS_G1_NC: case R_AARCH64_MOVW_UABS_G2: case R_AARCH64_MOVW_UABS_G2_NC:
		case R_AARCH64_MOVW_UABS_G3: case R_AARCH64_MOVW_SABS_G0: case R_AARCH64_MOVW_SABS_G1:
		case R_AARCH64_MOVW_SABS_G2:
			elf_patch(o, x, c);
			return;
		}
		elf_error(o, "Mach-O: relocation type %u to an absolute value", t);
		return;
	}
	int same = defined(a) && sym_msec(o, m, a) == ms;
	int64_t S = defined(a) ? (int64_t)sym_addr(o, m, a) : 0;
	switch (t) {
	case R_AARCH64_JUMP26: case R_AARCH64_CALL26:
		if (same) { elf_patch(o, x, S + c - (int64_t)P); return; }
		symrel(o, m, ms, raddr, aid, 1, 2, ARM64_RELOC_BRANCH26, c);
		return;
	case R_AARCH64_CONDBR19: case R_AARCH64_TSTBR14: case R_AARCH64_ADR_PREL_LO21: case R_AARCH64_LD_PREL_LO19:
	case R_AARCH64_PREL64: case R_AARCH64_PREL32: case R_AARCH64_PREL16:
		if (same) { elf_patch(o, x, S + c - (int64_t)P); return; }
		elf_error(o, "Mach-O: pc-relative relocation type %u to '%s' outside the section", t, a->name);
		return;
	case R_AARCH64_ADR_PREL_PG_HI21: case R_AARCH64_ADR_PREL_PG_HI21_NC:
		symrel(o, m, ms, raddr, aid, 1, 2, ARM64_RELOC_PAGE21, c);
		return;
	case R_AARCH64_ADD_ABS_LO12_NC: case R_AARCH64_LDST8_ABS_LO12_NC: case R_AARCH64_LDST16_ABS_LO12_NC:
	case R_AARCH64_LDST32_ABS_LO12_NC: case R_AARCH64_LDST64_ABS_LO12_NC: case R_AARCH64_LDST128_ABS_LO12_NC:
		symrel(o, m, ms, raddr, aid, 0, 2, ARM64_RELOC_PAGEOFF12, c);
		return;
	case R_AARCH64_ADR_GOT_PAGE:
		symrel(o, m, ms, raddr, aid, 1, 2, ARM64_RELOC_GOT_LOAD_PAGE21, c);
		return;
	case R_AARCH64_LD64_GOT_LO12_NC:
		symrel(o, m, ms, raddr, aid, 0, 2, ARM64_RELOC_GOT_LOAD_PAGEOFF12, c);
		return;
	case R_VCC_TLVP_PAGE21:
		symrel(o, m, ms, raddr, aid, 1, 2, ARM64_RELOC_TLVP_LOAD_PAGE21, c);
		return;
	case R_VCC_TLVP_PAGEOFF12:
		symrel(o, m, ms, raddr, aid, 0, 2, ARM64_RELOC_TLVP_LOAD_PAGEOFF12, c);
		return;
	case R_AARCH64_ABS64: case R_AARCH64_ABS32:
		/* addend in place */
		m->needsym[aid] = 1;
		addrel(o, m, ms, raddr, aid, 0, t == R_AARCH64_ABS64 ? 3 : 2, 1, ARM64_RELOC_UNSIGNED);
		put(x->p, (uint64_t)c, t == R_AARCH64_ABS64 ? 8 : 4);
		return;
	}
	elf_error(o, "Mach-O: unsupported relocation type %u", t);
}

static _Thread_local ElfObj *sort_o;	/* qsort has no context: per thread (vccd, vpp --batch) */
static int name_cmp(const void *pa, const void *pb)
{
	const ElfSym *a = elf_symp(sort_o, *(const uint32_t *)pa), *b = elf_symp(sort_o, *(const uint32_t *)pb);
	uint32_t n = a->len < b->len ? a->len : b->len;
	int r = memcmp(a->name, b->name, n);
	return r ? r : (int)a->len - (int)b->len;
}

static uint64_t al(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

size_t macho_finish(ElfObj *o)
{
	uint32_t ns = o->nsec;
	Mo *m = zalloc(o, sizeof *m);
	if (!m) return 0;
	o->fin = m;
	elf_layout(o);
	if (o->has_err) return 0;
	m->map = zalloc(o, ns + 1);
	m->vaddr = zalloc(o, sizeof(uint64_t) * (ns + 1));
	m->needsym = zalloc(o, o->nsym + 1);
	m->nlidx = zalloc(o, sizeof(uint32_t) * (o->nsym + 1));
	m->strx = zalloc(o, sizeof(uint32_t) * (o->nsym + 1));
	if (o->has_err) return 0;
	/* sections */
	uint64_t moffs[M_N] = { 0 };
	for (uint32_t s = 1; s < ns; s++) {
		int k = sec_map(o, s);
		m->map[s] = (int8_t)k;
	}
	for (int k = 0; k < M_N; k++) {
		uint64_t off = 0;
		uint32_t a = 0, any = 0;
		for (uint32_t s = 1; s < ns; s++) {
			if (m->map[s] != k) continue;
			ElfSec *se = elf_sec(o, s);
			off = al(off, (uint64_t)1 << se->align);
			m->vaddr[s] = off;	/* relative for now */
			off += se->size;
			if (se->align > a) a = se->align;
			any = 1;
		}
		m->msize[k] = off;
		m->malign[k] = a;
		moffs[k] = any;
	}
	{
		uint64_t addr = 0;
		uint32_t idx = 0;
		for (int k = 0; k < M_N; k++) {
			if (!moffs[k]) continue;
			addr = al(addr, (uint64_t)1 << m->malign[k]);
			m->maddr[k] = addr;
			m->midx[k] = ++idx;
			addr += m->msize[k];
		}
		m->nmsec = idx;
		for (uint32_t s = 1; s < ns; s++)
			if (m->map[s] >= 0) m->vaddr[s] += m->maddr[m->map[s]];
	}
	/* relocation buffers: at most two entries per fixup */
	{
		uint32_t cnt[M_N] = { 0 };
		ElfFix *fx = (ElfFix *)o->fixes.base;
		for (uint32_t i = 1; i < o->nfix; i++) {
			int k = m->map[elf_frag(o, fx[i].frag)->sec];
			if (k >= 0) cnt[k] += 2;
		}
		for (int k = 0; k < M_N; k++)
			if (cnt[k]) m->rel[k] = zalloc(o, sizeof(MRel) * cnt[k]);
		if (o->has_err) return 0;
	}
	{
		ElfFix *fx = (ElfFix *)o->fixes.base;
		for (uint32_t s = 1; s < ns; s++)
			for (uint32_t f = s; f; f = elf_frag(o, f)->next)
				for (uint32_t i = elf_frag(o, f)->fix_head; i; i = fx[i].next)
					resolve(o, m, &fx[i]);
	}
	if (o->has_err) return 0;
	/* symbols: locals, external definitions, undefined */
	m->nl = zalloc(o, sizeof(uint32_t) * (o->nsym + 1));
	if (!m->nl) return 0;
	uint32_t n = 0;
	for (int pass = 0; pass < 3; pass++) {
		uint32_t first = n;
		for (uint32_t i = 1; i < o->nsym; i++) {
			ElfSym *y = elf_symp(o, i);
			int def = defined(y), loc;
			if (y->flags & (ESYM_SECTION | ESYM_ABS)) continue;
			if (!y->len || y->name[0] == '$') continue;	/* mapping symbols */
			if (def && m->map[elf_frag(o, y->frag)->sec] < 0) continue;
			if (y->flags & (ESYM_TEMP | ESYM_NOSYMTAB)) {
				if (!m->needsym[i]) continue;
				if (!def) { elf_error(o, "undefined temporary symbol '%.*s'", (int)y->len, y->name); return 0; }
			}
			if (!def && !m->needsym[i] && !(y->flags & ESYM_USED) && !y->reg) continue;
			loc = def && y->bind == STB_LOCAL;
			if (pass == 0 ? !loc : pass == 1 ? (loc || !def) : def) continue;
			m->nl[n++] = i;
		}
		sort_o = o;
		if (pass > 0) qsort(m->nl + first, n - first, sizeof(uint32_t), name_cmp);
		if (pass == 0) m->nlocal = n - first;
		else if (pass == 1) m->nextdef = n - first;
		else m->nundef = n - first;
	}
	m->nnl = n;
	for (uint32_t k = 0; k < n; k++) m->nlidx[m->nl[k]] = k + 1;
	for (uint32_t i = 1; i < o->nsym; i++)
		if (m->needsym[i] && !m->nlidx[i]) {
			ElfSym *y = elf_symp(o, i);
			elf_error(o, "Mach-O: relocation against '%.*s', which has no symbol", (int)y->len, y->name);
			return 0;
		}
	/* strings: temporaries ".Lx" are written "lx" */
	{
		uint32_t size = 1;
		for (uint32_t k = 0; k < n; k++) size += elf_symp(o, m->nl[k])->len + 1;
		m->str = zalloc(o, size + 8);
		if (!m->str) return 0;
		uint32_t at = 1;
		for (uint32_t k = 0; k < n; k++) {
			ElfSym *y = elf_symp(o, m->nl[k]);
			m->strx[m->nl[k]] = at;
			if (y->len >= 2 && y->name[0] == '.' && y->name[1] == 'L') {
				m->str[at++] = 'l';
				memcpy(m->str + at, y->name + 2, y->len - 2);
				at += y->len - 2;
			} else {
				memcpy(m->str + at, y->name, y->len);
				at += y->len;
			}
			m->str[at++] = 0;
		}
		m->strsize = (at + 7) & ~7u;
	}
	/* file layout */
	m->cmdsize = (72 + 80 * (uint64_t)m->nmsec) + 24 /* build version */ + 24 /* symtab */ + 80 /* dysymtab */;
	uint64_t off = 32 + m->cmdsize;
	m->dataoff = off;
	for (int k = 0; k < M_N; k++) {
		if (!m->midx[k]) continue;
		if (mtab[k].flags == S_ZEROFILL || mtab[k].flags == S_THREAD_LOCAL_ZEROFILL) { m->moff[k] = 0; continue; }
		off = al(off, (uint64_t)1 << m->malign[k]);
		m->moff[k] = off;
		off += m->msize[k];
	}
	off = al(off, 8);
	for (int k = 0; k < M_N; k++) {
		if (!m->nrel[k]) continue;
		m->reloff[k] = (uint32_t)off;
		off += 8 * (uint64_t)m->nrel[k];
	}
	off = al(off, 8);
	m->symoff = off; off += 16 * (uint64_t)n;
	m->stroff = off; off += m->strsize;
	m->size = off;
	return off;
}

static void p32(uint8_t *p, uint64_t v) { put(p, v, 4); }
static void p64(uint8_t *p, uint64_t v) { put(p, v, 8); }
static void pname(uint8_t *p, const char *s) { memset(p, 0, 16); memcpy(p, s, strlen(s)); }

void macho_emit(ElfObj *o, uint8_t *b)
{
	Mo *m = o->fin;
	uint32_t ns = o->nsec;
	uint64_t vmsize = 0, fileend = m->dataoff;

	memset(b, 0, m->size);
	for (int k = 0; k < M_N; k++)
		if (m->midx[k]) {
			if (m->maddr[k] + m->msize[k] > vmsize) vmsize = m->maddr[k] + m->msize[k];
			if (m->moff[k] && m->moff[k] + m->msize[k] > fileend) fileend = m->moff[k] + m->msize[k];
		}
	/* header */
	p32(b, 0xfeedfacf); p32(b + 4, 0x0100000c); p32(b + 8, 0); p32(b + 12, 1 /* MH_OBJECT */);
	p32(b + 16, 4); p32(b + 20, m->cmdsize); p32(b + 24, 0); p32(b + 28, 0);
	uint8_t *c = b + 32;
	/* LC_SEGMENT_64 */
	p32(c, 0x19); p32(c + 4, 72 + 80 * m->nmsec);
	p64(c + 24, 0); p64(c + 32, vmsize); p64(c + 40, m->dataoff); p64(c + 48, fileend - m->dataoff);
	p32(c + 56, 7); p32(c + 60, 7); p32(c + 64, m->nmsec); p32(c + 68, 0);
	uint8_t *sh = c + 72;
	for (int k = 0; k < M_N; k++) {
		if (!m->midx[k]) continue;
		pname(sh, mtab[k].sect); pname(sh + 16, mtab[k].seg);
		p64(sh + 32, m->maddr[k]); p64(sh + 40, m->msize[k]);
		p32(sh + 48, m->moff[k]); p32(sh + 52, m->malign[k]);
		p32(sh + 56, m->nrel[k] ? m->reloff[k] : 0); p32(sh + 60, m->nrel[k]);
		p32(sh + 64, mtab[k].flags);
		sh += 80;
	}
	c = sh;
	/* LC_BUILD_VERSION: macOS 11.0 */
	p32(c, 0x32); p32(c + 4, 24); p32(c + 8, 1); p32(c + 12, 11u << 16); p32(c + 16, 0); p32(c + 20, 0);
	c += 24;
	/* LC_SYMTAB */
	p32(c, 0x2); p32(c + 4, 24); p32(c + 8, m->symoff); p32(c + 12, m->nnl); p32(c + 16, m->stroff); p32(c + 20, m->strsize);
	c += 24;
	/* LC_DYSYMTAB */
	p32(c, 0xb); p32(c + 4, 80);
	p32(c + 8, 0); p32(c + 12, m->nlocal);
	p32(c + 16, m->nlocal); p32(c + 20, m->nextdef);
	p32(c + 24, m->nlocal + m->nextdef); p32(c + 28, m->nundef);
	/* section data */
	for (uint32_t s = 1; s < ns; s++) {
		int k = m->map[s];
		ElfSec *se = elf_sec(o, s);
		if (k < 0 || !m->moff[k] || se->type == SHT_NOBITS) continue;
		uint8_t *d = b + m->moff[k] + (m->vaddr[s] - m->maddr[k]);
		for (uint32_t f = s; f; f = elf_frag(o, f)->next) {
			ElfFrag *fr = elf_frag(o, f);
			uint8_t *q = d + fr->base;
			for (ElfChunk *ch = fr->head; ch; ch = ch->next) { memcpy(q, ch->data, ch->used); q += ch->used; }
		}
	}
	/* relocations: symbol ids -> nlist indices */
	for (int k = 0; k < M_N; k++) {
		uint8_t *r = b + m->reloff[k];
		for (uint32_t i = 0; i < m->nrel[k]; i++, r += 8) {
			uint32_t w = m->rel[k][i].word;
			if (w >> 27 & 1)	/* extern: symnum is a symbol id */
				w = (w & 0xff000000u) | ((m->nlidx[w & 0xffffff] - 1) & 0xffffff);
			p32(r, m->rel[k][i].addr);
			p32(r + 4, w);
		}
	}
	/* nlist_64 */
	uint8_t *y = b + m->symoff;
	for (uint32_t k = 0; k < m->nnl; k++, y += 16) {
		ElfSym *s = elf_symp(o, m->nl[k]);
		int def = defined(s);
		uint8_t type = def ? N_SECT : 0;
		uint16_t desc = 0;
		if (k >= m->nlocal) type |= N_EXT;
		if (def && k >= m->nlocal && (s->vis == STV_HIDDEN || s->vis == STV_INTERNAL)) type |= N_PEXT;
		if (s->bind == STB_WEAK) desc |= def ? N_WEAK_DEF : N_WEAK_REF;
		p32(y, m->strx[m->nl[k]]);
		y[4] = type;
		y[5] = def ? (uint8_t)m->midx[sym_msec(o, m, s)] : 0;
		y[6] = (uint8_t)desc; y[7] = (uint8_t)(desc >> 8);
		p64(y + 8, def ? sym_addr(o, m, s) : 0);
	}
	memcpy(b + m->stroff, m->str, m->strsize);
}
