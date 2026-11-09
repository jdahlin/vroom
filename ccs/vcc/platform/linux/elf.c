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
#include "../../obj/objimpl.h"

/* Relocation records, kept per section in layout order. */
typedef struct { uint64_t off; uint32_t sym, type; int64_t addend; uint32_t sec, pad; } Rel;

typedef struct Out {
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

VPRIV void elf_reloc(ElfObj *o, Out *w, ElfFix *x, uint64_t P, uint32_t type, ElfSym *a, uint32_t aid, int64_t add)
{
	uint32_t s = elf_frag(o, x->frag)->sec;
	/* local symbols become section + offset, except (MC's shouldRelocateWithSymbol) in a
	 * SHF_MERGE section with an addend: the linker would find the piece at offset+addend,
	 * so "" + 1 could land in the next string */
	if (a && insec(a) && a->bind == STB_LOCAL && !(a->flags & ESYM_SECTION) && a->type != STT_GNU_IFUNC &&
	    !(add && (elf_sec(o, symsec(o, a))->flags & SHF_MERGE))) {
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

static size_t elf_finish_tail(ElfObj *o, Out *w);

size_t elf_finish(ElfObj *o)
{
	Out *w = rpush(&o->arena, sizeof *w, 8);
	*w = out_of(o);
	o->fin = w;
	uint32_t ns = o->nsec;
	elf_layout(o);
	/* fixups, in layout order */
	w->rel = rpush(&o->arena, sizeof(Rel) * (o->nfix + 1), 8);
	ElfFix *fx = (ElfFix *)o->fixes.base;
	for (uint32_t s = 1; s < ns; s++)
		for (uint32_t f = s; f; f = elf_frag(o, f)->next)
			for (uint32_t i = elf_frag(o, f)->fix_head; i; i = fx[i].next)
				elf_resolve(o, w, &fx[i]);
	return elf_finish_tail(o, w);
}

static size_t elf_finish_tail(ElfObj *o, Out *w)
{
	uint32_t ns = o->nsec;
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
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
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
