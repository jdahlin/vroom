/*
 * vlink for WebAssembly (PLAN-wasm.md, W8): links relocatable wasm objects
 * (tool-conventions Linking.md: clang's and vcc's) and archives of them into
 * a module, as wasm-ld does for emscripten: the command line emcc passes
 * (stub libraries of JS symbols, --export, -z stack-size, --stack-first,
 * --initial-heap, --table-base, ...), lazy archive members, weak symbols,
 * __wasm_call_ctors, the memory layout and the synthetic symbols
 * (__stack_pointer, __heap_base, __data_end, __start_/__stop_ sections),
 * garbage collection of functions and data from the roots, and relocations
 * applied in place (relocatable fields are padded LEBs).  wasm64 only.
 *
 * vlink runs this when called as wasm-ld or with -mwasm64.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int wasm_ld_main(int argc, char **argv);

/* ---------------- basics ---------------- */

#define GROW(p, n, c) \
	do { \
		if ((n) >= (c)) { \
			(c) = (c) ? (c) * 2 : 16; \
			(p) = realloc((p), (size_t)(c) * sizeof(*(p))); \
			if (!(p)) \
				abort(); \
		} \
	} while (0)

static int nerr;

__attribute__((format(printf, 1, 2))) static void err(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "wasm-ld: error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	nerr++;
}

typedef struct span {
	const uint8_t *p;
	uint32_t n;
} span;

static bool span_eq(span a, span b) { return a.n == b.n && !memcmp(a.p, b.p, a.n); }

static char *span_str(span s)
{
	char *r = malloc(s.n + 1);

	memcpy(r, s.p, s.n);
	r[s.n] = 0;
	return r;
}

typedef struct obuf {
	uint8_t *p;
	size_t n, cap;
} obuf;

static void ob_put(obuf *b, const void *s, size_t n)
{
	if (b->n + n > b->cap) {
		size_t c = b->cap ? b->cap * 2 : 65536;

		while (c < b->n + n)
			c *= 2;
		b->p = realloc(b->p, c);
		if (!b->p)
			abort();
		b->cap = c;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
}

static void ob8(obuf *b, uint8_t v) { ob_put(b, &v, 1); }

static void ouleb(obuf *b, uint64_t v)
{
	do {
		uint8_t c = v & 0x7f;

		v >>= 7;
		ob8(b, c | (v ? 0x80 : 0));
	} while (v);
}

static void osleb(obuf *b, int64_t v)
{
	for (;;) {
		uint8_t c = v & 0x7f;

		v >>= 7;
		if ((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40))) {
			ob8(b, c);
			return;
		}
		ob8(b, c | 0x80);
	}
}

static void ostr(obuf *b, const char *s)
{
	size_t n = strlen(s);

	ouleb(b, n);
	ob_put(b, s, n);
}

/* a reader over a byte range */
typedef struct rd {
	const uint8_t *p, *e;
	bool bad;
} rd;

static uint8_t r8(rd *r)
{
	if (r->p >= r->e) {
		r->bad = true;
		return 0;
	}
	return *r->p++;
}

static uint64_t ruleb(rd *r)
{
	uint64_t v = 0;
	int s = 0;
	uint8_t c;

	do {
		c = r8(r);
		if (s < 64)
			v |= (uint64_t)(c & 0x7f) << s;
		s += 7;
	} while ((c & 0x80) && !r->bad);
	return v;
}

static int64_t rsleb(rd *r)
{
	int64_t v = 0;
	int s = 0;
	uint8_t c;

	do {
		c = r8(r);
		if (s < 64)
			v |= (int64_t)((uint64_t)(c & 0x7f) << s);
		s += 7;
	} while ((c & 0x80) && !r->bad);
	if (s < 64 && (c & 0x40))
		v |= -((int64_t)1 << s);
	return v;
}

static span rbytes(rd *r, uint64_t n)
{
	span s = { r->p, 0 };

	if ((uint64_t)(r->e - r->p) < n) {
		r->bad = true;
		r->p = r->e;
		return s;
	}
	s.n = (uint32_t)n;
	r->p += n;
	return s;
}

static span rname(rd *r) { return rbytes(r, ruleb(r)); }

/* a constant expression (init expressions): skip to its end */
static span rexpr(rd *r)
{
	const uint8_t *s = r->p;

	for (;;) {
		uint8_t op = r8(r);

		if (r->bad)
			break;
		if (op == 0x0b)
			break;
		switch (op) {
		case 0x41: case 0x42: rsleb(r); break;
		case 0x43: rbytes(r, 4); break;
		case 0x44: rbytes(r, 8); break;
		case 0x23: ruleb(r); break;
		case 0xd2: ruleb(r); break;	/* ref.func */
		case 0xd0: r8(r); break;	/* ref.null */
		case 0x6a: case 0x6b: case 0x6c: case 0x7c: case 0x7d: case 0x7e: break;
		default: r->bad = true; break;
		}
	}
	return (span){ s, (uint32_t)(r->p - s) };
}

/* ---------------- the model of the inputs ---------------- */

enum { K_FUNCTION = 0, K_DATA = 1, K_GLOBAL = 2, K_SECTION = 3, K_TAG = 4, K_TABLE = 5 };
enum { F_WEAK = 0x1, F_LOCAL = 0x2, F_HIDDEN = 0x4, F_UNDEFINED = 0x10, F_EXPORTED = 0x20, F_EXPLICIT = 0x40,
       F_NO_STRIP = 0x80, F_TLS = 0x100, F_ABSOLUTE = 0x200 };

enum {
	R_FUNCTION_INDEX_LEB = 0, R_TABLE_INDEX_SLEB = 1, R_TABLE_INDEX_I32 = 2, R_MEMORY_ADDR_LEB = 3,
	R_MEMORY_ADDR_SLEB = 4, R_MEMORY_ADDR_I32 = 5, R_TYPE_INDEX_LEB = 6, R_GLOBAL_INDEX_LEB = 7,
	R_FUNCTION_OFFSET_I32 = 8, R_SECTION_OFFSET_I32 = 9, R_TAG_INDEX_LEB = 10, R_MEMORY_ADDR_REL_SLEB = 11,
	R_TABLE_INDEX_REL_SLEB = 12, R_GLOBAL_INDEX_I32 = 13, R_MEMORY_ADDR_LEB64 = 14, R_MEMORY_ADDR_SLEB64 = 15,
	R_MEMORY_ADDR_I64 = 16, R_MEMORY_ADDR_REL_SLEB64 = 17, R_TABLE_INDEX_SLEB64 = 18, R_TABLE_INDEX_I64 = 19,
	R_TABLE_NUMBER_LEB = 20, R_MEMORY_ADDR_TLS_SLEB = 21, R_FUNCTION_OFFSET_I64 = 22,
	R_MEMORY_ADDR_LOCREL_I32 = 23, R_TABLE_INDEX_REL_SLEB64 = 24, R_MEMORY_ADDR_TLS_SLEB64 = 25,
	R_FUNCTION_INDEX_I32 = 26,
};

static bool rel_addend(uint8_t t)
{
	switch (t) {
	case R_MEMORY_ADDR_LEB: case R_MEMORY_ADDR_SLEB: case R_MEMORY_ADDR_I32: case R_FUNCTION_OFFSET_I32:
	case R_SECTION_OFFSET_I32: case R_MEMORY_ADDR_REL_SLEB: case R_MEMORY_ADDR_LEB64: case R_MEMORY_ADDR_SLEB64:
	case R_MEMORY_ADDR_I64: case R_MEMORY_ADDR_REL_SLEB64: case R_MEMORY_ADDR_TLS_SLEB: case R_FUNCTION_OFFSET_I64:
	case R_MEMORY_ADDR_LOCREL_I32: case R_MEMORY_ADDR_TLS_SLEB64:
		return true;
	}
	return false;
}

typedef struct irel {
	uint8_t type;
	uint32_t off, idx;
	int64_t addend;
} irel;

typedef struct iimp {
	uint8_t kind;
	span module, field;
	uint32_t type;		/* function: type; global: valtype */
	uint8_t mut;
} iimp;

typedef struct ifunc {
	uint32_t type;
	span body;		/* locals and code */
	uint32_t off;		/* of body in the code section's payload */
	uint32_t rel0, nrel;	/* its relocations: crel[rel0 ..] */
	int32_t out;		/* output function index */
	bool live;
} ifunc;

typedef struct iglob {
	uint8_t type, mut;
	span init;
	int32_t out;
	bool live;
} iglob;

typedef struct iseg {
	span name;
	uint32_t p2align, flags;
	span data;
	uint32_t off;		/* of data in the data section's payload */
	uint32_t rel0, nrel;
	uint64_t addr;
	int32_t oseg;
	bool live;
} iseg;

struct gsym;

typedef struct isym {
	uint8_t kind;
	uint32_t flags;
	char *name;		/* NULL: unnamed */
	uint32_t index;		/* function/global/table/tag index */
	uint32_t seg;
	uint64_t off, size;
	struct gsym *g;		/* the global symbol (non-local) */
} isym;

typedef struct obj {
	char *name;
	span *types;
	uint32_t ntypes;
	iimp *imp;
	uint32_t nimp;
	uint32_t *fimp, nfimp;	/* function index < nfimp: the import */
	uint32_t *gimp, ngimp;
	uint32_t ntimp;
	uint32_t *tagimp, ntagimp;	/* tag index < ntagimp: the import */
	struct itag { uint32_t type; int32_t out; bool live; } *tag;
	uint32_t ntag;
	ifunc *fn;
	uint32_t nfn;
	iglob *gl;
	uint32_t ngl;
	iseg *seg;
	uint32_t nseg;
	isym *sym;
	uint32_t nsym;
	irel *crel, *drel;
	uint32_t ncrel, ndrel;
	struct { uint32_t prio, sym; } *init;
	uint32_t ninit;
	span features;
	bool loaded;
} obj;

/* global symbols */
enum { G_UNDEF, G_LAZY, G_DEFINED, G_SYNTH };

typedef struct gsym {
	char *name;
	uint32_t hash;
	uint8_t kind;		/* K_*, 255 unknown */
	uint8_t state;
	bool weak;		/* the definition is weak */
	bool strong_ref;	/* referenced by a non-weak undefined */
	bool referenced;
	bool stub;		/* provided at run time (a stub library) */
	bool live;
	bool required;		/* a root */
	obj *o;			/* the definition: o->sym[si] */
	uint32_t si;
	obj *uo;		/* an undefined reference (import module/field/type) */
	uint32_t usi;
	int ar;			/* lazy: archive, member */
	uint32_t member;
	uint64_t addr;		/* synthetic data: the address */
	int32_t out;		/* output function / global index */
	uint32_t slot;		/* table slot */
	char *deps;		/* stub: symbols it needs (comma-separated) */
	struct gsym *next;
} gsym;

static gsym **ght;
static uint32_t ghcap, ngsym;
static gsym **gall;
static uint32_t ngall, cgall;

static uint32_t hname(const char *s, size_t n)
{
	uint32_t h = 2166136261u;

	for (size_t i = 0; i < n; i++)
		h = (h ^ (uint8_t)s[i]) * 16777619u;
	return h;
}

static gsym *gfind(const char *s, size_t n, bool create)
{
	uint32_t h = hname(s, n);
	gsym *g;

	if (!ght) {
		ghcap = 1 << 16;
		ght = calloc(ghcap, sizeof *ght);
	}
	for (g = ght[h & (ghcap - 1)]; g; g = g->next)
		if (g->hash == h && strlen(g->name) == n && !memcmp(g->name, s, n))
			return g;
	if (!create)
		return NULL;
	g = calloc(1, sizeof *g);
	g->name = malloc(n + 1);
	memcpy(g->name, s, n);
	g->name[n] = 0;
	g->hash = h;
	g->kind = 255;
	g->ar = -1;
	g->out = -1;
	g->next = ght[h & (ghcap - 1)];
	ght[h & (ghcap - 1)] = g;
	ngsym++;
	GROW(gall, ngall, cgall);
	gall[ngall++] = g;
	return g;
}

static gsym *gname(const char *s) { return gfind(s, strlen(s), true); }

/* ---------------- inputs ---------------- */

typedef struct archive {
	char *name;
	const uint8_t *p;
	size_t n;
	const char *longnames;
	size_t nlong;
	uint32_t *loaded;	/* members loaded (by offset), a small set */
	uint32_t nloaded, cloaded;
} archive;

static obj **objs;
static uint32_t nobjs, cobjs;
static archive *ars;
static uint32_t nars, cars;

static const uint8_t *map_file(const char *path, size_t *n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	struct stat st;
	void *p;

	if (fd < 0)
		return NULL;
	if (fstat(fd, &st)) {
		close(fd);
		return NULL;
	}
	*n = (size_t)st.st_size;
	p = *n ? mmap(NULL, *n, PROT_READ, MAP_PRIVATE, fd, 0) : (void *)"";
	close(fd);
	return p == MAP_FAILED ? NULL : p;
}

static void parse_linking(obj *o, rd *r)
{
	if (ruleb(r) != 2) {
		err("%s: linking section version not 2", o->name);
		return;
	}
	while (r->p < r->e && !r->bad) {
		uint8_t t = r8(r);
		uint64_t len = ruleb(r);
		rd s = { r->p, r->p + len, false };

		r->p += len;
		if (t == 8) {
			uint32_t n = (uint32_t)ruleb(&s);

			o->sym = calloc(n + 1, sizeof(isym));
			o->nsym = n;
			for (uint32_t i = 0; i < n && !s.bad; i++) {
				isym *y = &o->sym[i];

				y->kind = r8(&s);
				y->flags = (uint32_t)ruleb(&s);
				switch (y->kind) {
				case K_FUNCTION: case K_GLOBAL: case K_TABLE: case K_TAG:
					y->index = (uint32_t)ruleb(&s);
					if (!(y->flags & F_UNDEFINED) || (y->flags & F_EXPLICIT))
						y->name = span_str(rname(&s));
					else {
						/* the import's field */
						uint32_t k = y->kind == K_FUNCTION && y->index < o->nfimp ? o->fimp[y->index]
							   : y->kind == K_GLOBAL && y->index < o->ngimp ? o->gimp[y->index]
							   : y->kind == K_TAG && y->index < o->ntagimp ? o->tagimp[y->index] : UINT32_MAX;

						if (k != UINT32_MAX)
							y->name = span_str(o->imp[k].field);
						else if (y->kind == K_TABLE)
							y->name = span_str((span){ (const uint8_t *)"__indirect_function_table", 25 });
					}
					break;
				case K_DATA:
					y->name = span_str(rname(&s));
					if (!(y->flags & F_UNDEFINED)) {
						y->seg = (uint32_t)ruleb(&s);
						y->off = ruleb(&s);
						y->size = ruleb(&s);
					}
					break;
				case K_SECTION:
					y->index = (uint32_t)ruleb(&s);
					break;
				default:
					s.bad = true;
				}
			}
		} else if (t == 5) {
			uint32_t n = (uint32_t)ruleb(&s);

			for (uint32_t i = 0; i < n && !s.bad; i++) {
				span nm = rname(&s);
				uint32_t al = (uint32_t)ruleb(&s), fl = (uint32_t)ruleb(&s);

				if (i < o->nseg) {
					o->seg[i].name = nm;
					o->seg[i].p2align = al;
					o->seg[i].flags = fl;
				}
			}
		} else if (t == 6) {
			uint32_t n = (uint32_t)ruleb(&s);

			o->init = calloc(n + 1, sizeof *o->init);
			for (uint32_t i = 0; i < n && !s.bad; i++) {
				o->init[i].prio = (uint32_t)ruleb(&s);
				o->init[i].sym = (uint32_t)ruleb(&s);
			}
			o->ninit = n;
		} else if (t == 7) {
			if (ruleb(&s))
				err("%s: COMDATs are not supported yet", o->name);
		}
		if (s.bad)
			r->bad = true;
	}
}

static int cmp_rel(const void *a, const void *b)
{
	const irel *x = a, *y = b;

	return x->off < y->off ? -1 : x->off > y->off;
}

static obj *parse_obj(const char *name, const uint8_t *p, size_t n)
{
	obj *o = calloc(1, sizeof *o);
	rd r = { p + 8, p + n, false };
	uint32_t secidx = 0, code_idx = UINT32_MAX, data_idx = UINT32_MAX;
	const uint8_t *code0 = NULL, *data0 = NULL;
	struct { uint32_t sec; const uint8_t *p, *e; } rels[8];
	uint32_t nrels = 0;

	o->name = strdup(name);
	if (n < 8 || memcmp(p, "\0asm\1\0\0\0", 8)) {
		err("%s: not a wasm object", name);
		return o;
	}
	while (r.p < r.e && !r.bad) {
		uint8_t id = r8(&r);
		uint64_t len = ruleb(&r);
		rd s = { r.p, r.p + len, false };

		if (len > (uint64_t)(r.e - r.p)) {
			r.bad = true;
			break;
		}
		r.p += len;
		switch (id) {
		case 0: {
			span nm = rname(&s);

			if (nm.n == 7 && !memcmp(nm.p, "linking", 7))
				parse_linking(o, &s);
			else if (nm.n > 6 && !memcmp(nm.p, "reloc.", 6)) {
				if (nrels < 8) {
					rels[nrels].sec = (uint32_t)ruleb(&s);
					rels[nrels].p = s.p;
					rels[nrels].e = s.e;
					nrels++;
				}
			} else if (nm.n == 15 && !memcmp(nm.p, "target_features", 15))
				o->features = (span){ s.p, (uint32_t)(s.e - s.p) };
			break;
		}
		case 1: {
			uint32_t k = (uint32_t)ruleb(&s);

			o->types = calloc(k + 1, sizeof(span));
			o->ntypes = k;
			for (uint32_t i = 0; i < k && !s.bad; i++) {
				const uint8_t *t0;

				if (r8(&s) != 0x60)
					s.bad = true;
				t0 = s.p;
				rbytes(&s, ruleb(&s));
				rbytes(&s, ruleb(&s));
				o->types[i] = (span){ t0, (uint32_t)(s.p - t0) };
			}
			break;
		}
		case 2: {
			uint32_t k = (uint32_t)ruleb(&s);

			o->imp = calloc(k + 1, sizeof(iimp));
			o->fimp = calloc(k + 1, sizeof(uint32_t));
			o->gimp = calloc(k + 1, sizeof(uint32_t));
			o->tagimp = calloc(k + 1, sizeof(uint32_t));
			o->nimp = k;
			for (uint32_t i = 0; i < k && !s.bad; i++) {
				iimp *m = &o->imp[i];

				m->module = rname(&s);
				m->field = rname(&s);
				m->kind = r8(&s);
				switch (m->kind) {
				case 0:
					m->type = (uint32_t)ruleb(&s);
					o->fimp[o->nfimp++] = i;
					break;
				case 1: {
					uint8_t fl;

					r8(&s);
					fl = r8(&s);
					ruleb(&s);
					if (fl & 1)
						ruleb(&s);
					o->ntimp++;
					break;
				}
				case 2: {
					uint8_t fl = r8(&s);

					ruleb(&s);
					if (fl & 1)
						ruleb(&s);
					break;
				}
				case 3:
					m->type = r8(&s);
					m->mut = r8(&s);
					o->gimp[o->ngimp++] = i;
					break;
				case 4:
					r8(&s);
					m->type = (uint32_t)ruleb(&s);
					o->tagimp[o->ntagimp++] = i;
					break;
				default:
					s.bad = true;
				}
			}
			break;
		}
		case 3: {
			uint32_t k = (uint32_t)ruleb(&s);

			o->fn = calloc(k + 1, sizeof(ifunc));
			o->nfn = k;
			for (uint32_t i = 0; i < k; i++) {
				o->fn[i].type = (uint32_t)ruleb(&s);
				o->fn[i].out = -1;
			}
			break;
		}
		case 6: {
			uint32_t k = (uint32_t)ruleb(&s);

			o->gl = calloc(k + 1, sizeof(iglob));
			o->ngl = k;
			for (uint32_t i = 0; i < k && !s.bad; i++) {
				o->gl[i].type = r8(&s);
				o->gl[i].mut = r8(&s);
				o->gl[i].init = rexpr(&s);
				o->gl[i].out = -1;
			}
			break;
		}
		case 10: {
			uint32_t k = (uint32_t)ruleb(&s);

			code_idx = secidx;
			code0 = s.p - 0;	/* the payload starts at the count */
			code0 = r.p - len;
			if (k != o->nfn)
				err("%s: code and function sections disagree", name);
			for (uint32_t i = 0; i < k && i < o->nfn && !s.bad; i++) {
				uint64_t sz = ruleb(&s);

				o->fn[i].off = (uint32_t)(s.p - code0);
				o->fn[i].body = rbytes(&s, sz);
			}
			break;
		}
		case 11: {
			uint32_t k = (uint32_t)ruleb(&s);

			data_idx = secidx;
			data0 = r.p - len;
			o->seg = calloc(k + 1, sizeof(iseg));
			o->nseg = k;
			for (uint32_t i = 0; i < k && !s.bad; i++) {
				uint64_t fl = ruleb(&s);

				if (fl == 2)
					ruleb(&s);
				if (fl != 1)
					rexpr(&s);
				o->seg[i].flags = 0;
				o->seg[i].oseg = -1;
				{
					uint64_t sz = ruleb(&s);

					o->seg[i].off = (uint32_t)(s.p - data0);
					o->seg[i].data = rbytes(&s, sz);
				}
			}
			break;
		}
		case 13: {
			uint32_t k = (uint32_t)ruleb(&s);

			o->tag = calloc(k + 1, sizeof *o->tag);
			o->ntag = k;
			for (uint32_t i = 0; i < k && !s.bad; i++) {
				r8(&s);		/* attribute: exception */
				o->tag[i].type = (uint32_t)ruleb(&s);
				o->tag[i].out = -1;
			}
			break;
		}
		case 4: case 5: case 7: case 8:
			err("%s: section %u in an object is not supported", name, id);
			break;
		default:
			break;
		}
		if (s.bad)
			r.bad = true;
		secidx++;
	}
	if (r.bad)
		err("%s: malformed object", name);
	for (uint32_t k = 0; k < nrels; k++) {
		rd s = { rels[k].p, rels[k].e, false };
		uint32_t cnt = (uint32_t)ruleb(&s);
		irel *v = calloc(cnt + 1, sizeof(irel));

		for (uint32_t i = 0; i < cnt && !s.bad; i++) {
			v[i].type = r8(&s);
			v[i].off = (uint32_t)ruleb(&s);
			v[i].idx = (uint32_t)ruleb(&s);
			if (rel_addend(v[i].type))
				v[i].addend = rsleb(&s);
		}
		qsort(v, cnt, sizeof(irel), cmp_rel);
		if (rels[k].sec == code_idx) {
			o->crel = v;
			o->ncrel = cnt;
		} else if (rels[k].sec == data_idx) {
			o->drel = v;
			o->ndrel = cnt;
		} else {
			free(v);	/* debug sections: dropped */
		}
	}
	/* each function's and segment's relocations */
	for (uint32_t i = 0, j = 0; i < o->nfn; i++) {
		while (j < o->ncrel && o->crel[j].off < o->fn[i].off)
			j++;
		o->fn[i].rel0 = j;
		while (j < o->ncrel && o->crel[j].off < o->fn[i].off + o->fn[i].body.n)
			j++;
		o->fn[i].nrel = j - o->fn[i].rel0;
	}
	for (uint32_t i = 0, j = 0; i < o->nseg; i++) {
		while (j < o->ndrel && o->drel[j].off < o->seg[i].off)
			j++;
		o->seg[i].rel0 = j;
		while (j < o->ndrel && o->drel[j].off < o->seg[i].off + o->seg[i].data.n)
			j++;
		o->seg[i].nrel = j - o->seg[i].rel0;
	}
	return o;
}

/* ---------------- symbol resolution ---------------- */

static void load_member(int a, uint32_t off);

static bool sym_defined(const obj *o, const isym *y)
{
	if (y->flags & F_UNDEFINED)
		return false;
	(void)o;
	return true;
}

/* an undefined reference: pull a lazy member */
static void reference(gsym *g, obj *o, uint32_t si, bool weak)
{
	if (!g->uo) {
		g->uo = o;
		g->usi = si;
	}
	g->referenced = true;
	if (!weak)
		g->strong_ref = true;
	if (g->state == G_LAZY && !weak) {
		g->state = G_UNDEF;
		load_member(g->ar, g->member);
	}
}

static void add_obj(obj *o)
{
	GROW(objs, nobjs, cobjs);
	objs[nobjs++] = o;
	o->loaded = true;
	for (uint32_t i = 0; i < o->nsym; i++) {
		isym *y = &o->sym[i];
		gsym *g;

		if ((y->flags & F_LOCAL) || !y->name || y->kind == K_SECTION)
			continue;
		g = gname(y->name);
		y->g = g;
		if (g->kind == 255)
			g->kind = y->kind;
		else if (g->kind != y->kind && g->state != G_LAZY)
			err("symbol type mismatch: %s (%s)", g->name, o->name);
		if (sym_defined(o, y)) {
			bool w = (y->flags & F_WEAK) != 0;

			if (g->state == G_DEFINED) {
				if (!w && !g->weak) {
					err("duplicate symbol: %s\n>>> defined in %s\n>>> defined in %s", g->name,
					    g->o->name, o->name);
					continue;
				}
				if (w || !g->weak)
					continue;
			}
			g->state = G_DEFINED;
			g->o = o;
			g->si = i;
			g->weak = w;
			g->kind = y->kind;
		} else {
			reference(g, o, i, (y->flags & F_WEAK) != 0);
		}
	}
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static uint64_t ar_size(const uint8_t *h)
{
	char t[11];

	memcpy(t, h + 48, 10);
	t[10] = 0;
	return strtoull(t, NULL, 10);
}

static void load_member(int a, uint32_t off)
{
	archive *A = &ars[a];
	const uint8_t *h = A->p + off;
	uint64_t sz;
	char nm[512];
	char full[1024];

	for (uint32_t i = 0; i < A->nloaded; i++)
		if (A->loaded[i] == off)
			return;
	GROW(A->loaded, A->nloaded, A->cloaded);
	A->loaded[A->nloaded++] = off;
	if (off + 60 > A->n) {
		err("%s: bad member offset", A->name);
		return;
	}
	sz = ar_size(h);
	if (h[0] == '/' && h[1] >= '0' && h[1] <= '9' && A->longnames) {
		size_t lo = strtoul((const char *)h + 1, NULL, 10), k = 0;

		while (lo + k < A->nlong && A->longnames[lo + k] != '/' && A->longnames[lo + k] != '\n' && k < sizeof nm - 1)
			nm[k] = A->longnames[lo + k], k++;
		nm[k] = 0;
	} else {
		size_t k = 0;

		while (k < 16 && h[k] != '/' && h[k] != ' ')
			nm[k] = (char)h[k], k++;
		nm[k] = 0;
	}
	snprintf(full, sizeof full, "%s(%s)", A->name, nm);
	add_obj(parse_obj(full, h + 60, (size_t)sz));
}

static void add_archive(const char *name, const uint8_t *p, size_t n)
{
	archive *A;
	size_t off = 8;
	int a;

	GROW(ars, nars, cars);
	a = (int)nars++;
	A = &ars[a];
	memset(A, 0, sizeof *A);
	A->name = strdup(name);
	A->p = p;
	A->n = n;
	/* the long-name table first */
	for (size_t o2 = 8; o2 + 60 <= n;) {
		uint64_t sz = ar_size(p + o2);

		if (!memcmp(p + o2, "//              ", 16)) {
			A->longnames = (const char *)p + o2 + 60;
			A->nlong = (size_t)sz;
			break;
		}
		if (memcmp(p + o2, "/ ", 2) && memcmp(p + o2, "/SYM64/", 7))
			break;
		o2 += 60 + sz + (sz & 1);
	}
	if (off + 60 > n || (memcmp(p + off, "/ ", 2) && memcmp(p + off, "/SYM64/", 7))) {
		err("%s: archive without a symbol table", name);
		return;
	}
	{
		const uint8_t *s = p + off + 60;
		uint64_t sz = ar_size(p + off);
		bool w64 = !memcmp(p + off, "/SYM64/", 7);
		uint64_t cnt = w64 ? ((uint64_t)be32(s) << 32 | be32(s + 4)) : be32(s);
		const uint8_t *offs = s + (w64 ? 8 : 4);
		const char *names = (const char *)offs + cnt * (w64 ? 8 : 4), *end = (const char *)s + sz;

		for (uint64_t i = 0; i < cnt && names < end; i++) {
			uint32_t mo = w64 ? (uint32_t)((uint64_t)be32(offs + 8 * i) << 32 | be32(offs + 8 * i + 4)) : be32(offs + 4 * i);
			size_t len = strnlen(names, (size_t)(end - names));
			gsym *g = gfind(names, len, true);

			names += len + 1;
			if (g->state == G_UNDEF && g->strong_ref) {
				load_member(a, mo);
			} else if (g->state == G_UNDEF && !g->referenced) {
				g->state = G_LAZY;
				g->ar = a;
				g->member = mo;
			}
		}
	}
}

/* ---------------- options ---------------- */

static struct {
	const char *out;
	uint64_t stack_size, initial_heap, initial_memory, max_memory, global_base, table_base;
	bool stack_first, export_table, no_growable, growable_table, allow_undefined, no_entry, gc;
	const char **exports, **exports_ifdef;
	uint32_t nexp, cexp, nexpi, cexpi;
	const char **libdirs;
	uint32_t nlib, clib;
	const char **features;
	uint32_t nfeat, cfeat;
	const char *entry;
} opt = { .stack_size = 65536, .global_base = 1024, .table_base = 1, .gc = true };

static uint64_t num(const char *s)
{
	char *e;
	uint64_t v = strtoull(s, &e, 0);

	if (*e)
		err("bad number '%s'", s);
	return v;
}

static int load_path(const char *path)
{
	size_t n;
	const uint8_t *p = map_file(path, &n);

	if (!p) {
		err("cannot open %s: %s", path, strerror(errno));
		return -1;
	}
	if (n >= 8 && !memcmp(p, "!<arch>\n", 8))
		add_archive(path, p, n);
	else if (n >= 5 && !memcmp(p, "#STUB", 5)) {
		/* a stub library: "sym: dep,dep" per line, symbols provided at run time */
		const char *s = (const char *)p, *e = s + n;

		while (s < e) {
			const char *l = memchr(s, '\n', (size_t)(e - s)), *le = l ? l : e, *c;

			if (*s != '#' && (c = memchr(s, ':', (size_t)(le - s)))) {
				gsym *g = gfind(s, (size_t)(c - s), true);
				const char *d = c + 1;

				g->stub = true;
				while (d < le && *d == ' ')
					d++;
				if (d < le) {
					g->deps = malloc((size_t)(le - d) + 1);
					memcpy(g->deps, d, (size_t)(le - d));
					g->deps[le - d] = 0;
				}
			}
			s = le + 1;
		}
	} else {
		add_obj(parse_obj(path, p, n));
	}
	return 0;
}

static int load_lib(const char *l)
{
	char path[4096];

	for (uint32_t i = 0; i < opt.nlib; i++) {
		snprintf(path, sizeof path, "%s/lib%s.a", opt.libdirs[i], l);
		if (!access(path, R_OK))
			return load_path(path);
	}
	err("unable to find library -l%s", l);
	return -1;
}

/* ---------------- liveness ---------------- */

typedef struct tgt {
	uint8_t kind;
	obj *o;			/* defined in o: function/global index, data symbol */
	uint32_t idx;
	isym *y;		/* data: the symbol */
	gsym *g;		/* undefined or synthetic */
} tgt;

/* where o->sym[si] resolves to */
static tgt resolve(obj *o, uint32_t si)
{
	isym *y = &o->sym[si];
	tgt t = { y->kind, NULL, 0, NULL, NULL };

	if (y->g) {
		gsym *g = y->g;

		if (g->state == G_DEFINED) {
			o = g->o;
			y = &o->sym[g->si];
		} else {
			t.g = g;
			return t;
		}
	} else if (y->flags & F_UNDEFINED) {
		/* a local undefined (an import without a symbol name) */
		t.g = NULL;
		return t;
	}
	t.o = o;
	t.y = y;
	if (y->kind == K_FUNCTION)
		t.idx = y->index - o->nfimp;
	else if (y->kind == K_GLOBAL)
		t.idx = y->index - o->ngimp;
	else if (y->kind == K_TAG)
		t.idx = y->index - o->ntagimp;
	else
		t.idx = y->index;
	return t;
}

typedef struct work {
	obj *o;
	uint32_t i;
	uint8_t what;		/* 0 function, 1 segment */
} work;

static work *wl;
static uint32_t nwl, cwl;
static bool ctors_live;

static void mark_tgt(tgt t);

static void mark_sym(obj *o, uint32_t si) { mark_tgt(resolve(o, si)); }

static void mark_gsym(gsym *g)
{
	if (g->state == G_DEFINED)
		mark_sym(g->o, g->si);
	else if (!g->live) {
		g->live = true;
		if (!strcmp(g->name, "__wasm_call_ctors"))
			ctors_live = true;
		if (g->stub && g->deps) {
			/* what the run-time implementation calls back */
			char *d = g->deps;

			while (*d) {
				char *c = strchr(d, ',');
				size_t len = c ? (size_t)(c - d) : strlen(d);
				gsym *x = gfind(d, len, true);

				x->required = true;
				mark_gsym(x);
				d += len + (c ? 1 : 0);
			}
		}
	}
}

static void mark_tgt(tgt t)
{
	if (t.g) {
		mark_gsym(t.g);
		return;
	}
	if (!t.o)
		return;
	if (t.kind == K_FUNCTION && t.idx < t.o->nfn && !t.o->fn[t.idx].live) {
		t.o->fn[t.idx].live = true;
		GROW(wl, nwl, cwl);
		wl[nwl++] = (work){ t.o, t.idx, 0 };
	} else if (t.kind == K_DATA && t.y && t.y->seg < t.o->nseg && !t.o->seg[t.y->seg].live) {
		t.o->seg[t.y->seg].live = true;
		GROW(wl, nwl, cwl);
		wl[nwl++] = (work){ t.o, t.y->seg, 1 };
	} else if (t.kind == K_GLOBAL && t.idx < t.o->ngl) {
		t.o->gl[t.idx].live = true;
	} else if (t.kind == K_TAG && t.idx < t.o->ntag) {
		t.o->tag[t.idx].live = true;
	}
}

static void mark_rels(obj *o, irel *r, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		if (r[i].type == R_TYPE_INDEX_LEB)
			continue;
		if (r[i].idx < o->nsym)
			mark_sym(o, r[i].idx);
	}
}

static void gc(void)
{
	/* roots: exports, no-strip symbols, constructors when __wasm_call_ctors lives */
	bool more = true;

	for (uint32_t i = 0; i < opt.nexp; i++)
		mark_gsym(gname(opt.exports[i]));
	for (uint32_t i = 0; i < opt.nexpi; i++) {
		gsym *g = gfind(opt.exports_ifdef[i], strlen(opt.exports_ifdef[i]), false);

		if (g && g->state == G_DEFINED)
			mark_gsym(g);
	}
	for (uint32_t k = 0; k < nobjs; k++) {
		obj *o = objs[k];

		for (uint32_t i = 0; i < o->nsym; i++)
			if ((o->sym[i].flags & F_NO_STRIP) && !(o->sym[i].flags & F_UNDEFINED))
				mark_sym(o, i);
		if (!opt.gc) {
			for (uint32_t i = 0; i < o->nfn; i++)
				mark_tgt((tgt){ K_FUNCTION, o, i, NULL, NULL });
		}
	}
	while (more) {
		more = false;
		while (nwl) {
			work w = wl[--nwl];

			if (w.what == 0)
				mark_rels(w.o, w.o->crel + w.o->fn[w.i].rel0, w.o->fn[w.i].nrel);
			else
				mark_rels(w.o, w.o->drel + w.o->seg[w.i].rel0, w.o->seg[w.i].nrel);
		}
		if (ctors_live)
			for (uint32_t k = 0; k < nobjs; k++)
				for (uint32_t i = 0; i < objs[k]->ninit; i++) {
					tgt t = resolve(objs[k], objs[k]->init[i].sym);

					if (t.o && t.kind == K_FUNCTION && !t.o->fn[t.idx].live) {
						mark_tgt(t);
						more = true;
					}
				}
	}
}

/* ---------------- output ---------------- */

typedef struct oseg {
	char *name;
	uint32_t p2align;
	uint64_t addr, size;
	bool tls, bss;
	int order;
} oseg;

static span *otypes;
static uint32_t notypes, cotypes;

static uint32_t otype(span t)
{
	for (uint32_t i = 0; i < notypes; i++)
		if (span_eq(otypes[i], t))
			return i;
	GROW(otypes, notypes, cotypes);
	otypes[notypes] = t;
	return notypes++;
}

static const char *oseg_name(span n, char *buf, size_t cap)
{
	static const char *const pre[] = { ".text", ".data", ".bss", ".rodata", ".tdata", ".tbss", ".init_array" };

	for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++) {
		size_t k = strlen(pre[i]);

		if (n.n >= k && !memcmp(n.p, pre[i], k) && (n.n == k || n.p[k] == '.'))
			return pre[i];
	}
	snprintf(buf, cap, "%.*s", (int)n.n, n.p);
	return buf;
}

static int seg_order(const char *n)
{
	if (!strcmp(n, ".tdata") || !strcmp(n, ".tbss"))
		return 0;
	if (!strcmp(n, ".rodata"))
		return 1;
	if (!strcmp(n, ".data"))
		return 2;
	if (!strcmp(n, ".bss"))
		return 4;
	return 3;
}

static void pad_uleb(uint8_t *p, uint64_t v, int n)
{
	for (int i = 0; i < n; i++) {
		p[i] = (v & 0x7f) | (i + 1 < n ? 0x80 : 0);
		v >>= 7;
	}
}

static void pad_sleb(uint8_t *p, int64_t v, int n)
{
	for (int i = 0; i < n; i++) {
		p[i] = (v & 0x7f) | (i + 1 < n ? 0x80 : 0);
		v >>= 7;
	}
}

static size_t sec_begin(obuf *b, uint8_t id, const char *name)
{
	size_t at;

	ob8(b, id);
	at = b->n;
	ob_put(b, "\0\0\0\0\0", 5);
	if (name)
		ostr(b, name);
	return at;
}

static void sec_end(obuf *b, size_t at) { pad_uleb(b->p + at, b->n - at - 5, 5); }

/* synthetic symbols */
static uint64_t stack_low, stack_high, data_end, heap_base, heap_end, mem_pages, max_pages, tls_base, tls_size,
	tls_align = 1;
static int32_t sp_global = 0;
static uint32_t nslots;
static oseg *osegs;
static uint32_t nosegs;

static uint32_t slot_of(gsym *g, obj *o, uint32_t fidx);

/* the table slot of a function: kept on the definition's global symbol, or a side table for locals */
typedef struct lslot {
	obj *o;
	uint32_t f, slot;
} lslot;
static lslot *lslots;
static uint32_t nlslots, clslots;
static uint32_t *slot_func;	/* slot - table_base -> output function index */
static uint32_t cslot_func;

static uint32_t slot_of(gsym *g, obj *o, uint32_t fidx)
{
	int32_t out;

	if (g && g->state != G_DEFINED) {
		if (g->slot)
			return g->slot;
		if (g->out < 0 || (g->state == G_SYNTH && strcmp(g->name, "__wasm_call_ctors")))
			return 0;	/* an undefined weak function: null (calls go to a stub that traps) */
		out = g->out;
		GROW(slot_func, nslots, cslot_func);
		slot_func[nslots++] = (uint32_t)out;
		return g->slot = (uint32_t)(opt.table_base + nslots - 1);
	}
	for (uint32_t i = 0; i < nlslots; i++)
		if (lslots[i].o == o && lslots[i].f == fidx)
			return lslots[i].slot;
	GROW(lslots, nlslots, clslots);
	GROW(slot_func, nslots, cslot_func);
	slot_func[nslots++] = (uint32_t)o->fn[fidx].out;
	lslots[nlslots] = (lslot){ o, fidx, (uint32_t)(opt.table_base + nslots - 1) };
	return lslots[nlslots++].slot;
}

static bool synth_data(gsym *g, uint64_t *a)
{
	const char *n = g->name;

	if (!strcmp(n, "__heap_base")) *a = heap_base;
	else if (!strcmp(n, "__heap_end")) *a = heap_end;
	else if (!strcmp(n, "__data_end")) *a = data_end;
	else if (!strcmp(n, "__stack_low")) *a = stack_low;
	else if (!strcmp(n, "__stack_high")) *a = stack_high;
	else if (!strcmp(n, "__global_base")) *a = opt.global_base;
	else if (!strcmp(n, "__dso_handle")) *a = 0;
	else if (!strncmp(n, "__start_", 8) || !strncmp(n, "__stop_", 7)) {
		const char *s = n + (n[3] == 'a' ? 8 : 7);

		for (uint32_t i = 0; i < nosegs; i++)
			if (!strcmp(osegs[i].name, s)) {
				*a = osegs[i].addr + (n[3] == 'a' ? 0 : osegs[i].size);
				return true;
			}
		return false;
	} else
		return false;
	return true;
}

/* the value a relocation puts in place */
static int rel_value(obj *o, irel *r, uint64_t *v, uint32_t *imm_globals)
{
	tgt t;

	if (r->type == R_TYPE_INDEX_LEB) {
		*v = otype(o->types[r->idx]);
		return 0;
	}
	if (r->idx >= o->nsym) {
		err("%s: relocation symbol %u out of range", o->name, r->idx);
		return -1;
	}
	t = resolve(o, r->idx);
	switch (r->type) {
	case R_FUNCTION_INDEX_LEB: case R_FUNCTION_INDEX_I32:
		if (t.g) {
			if (t.g->out < 0) {
				err("%s: undefined symbol: %s", o->name, t.g->name);
				return -1;
			}
			*v = (uint64_t)t.g->out;
		} else {
			*v = (uint64_t)t.o->fn[t.idx].out;
		}
		return 0;
	case R_TABLE_INDEX_SLEB: case R_TABLE_INDEX_I32: case R_TABLE_INDEX_SLEB64: case R_TABLE_INDEX_I64:
		*v = t.g ? slot_of(t.g, NULL, 0) : slot_of(NULL, t.o, t.idx);
		return 0;
	case R_MEMORY_ADDR_LEB: case R_MEMORY_ADDR_SLEB: case R_MEMORY_ADDR_I32: case R_MEMORY_ADDR_LEB64:
	case R_MEMORY_ADDR_SLEB64: case R_MEMORY_ADDR_I64: case R_MEMORY_ADDR_TLS_SLEB: case R_MEMORY_ADDR_TLS_SLEB64: {
		uint64_t a = 0;

		if (t.g) {
			if (t.g->state == G_SYNTH)
				a = t.g->addr;
			else if (!synth_data(t.g, &a) && !(t.g->weak || !t.g->strong_ref)) {
				err("%s: undefined symbol: %s", o->name, t.g->name);
				return -1;
			}
		} else if (t.y) {
			a = t.o->seg[t.y->seg].addr + t.y->off;
		}
		a += (uint64_t)r->addend;
		if (r->type == R_MEMORY_ADDR_TLS_SLEB || r->type == R_MEMORY_ADDR_TLS_SLEB64)
			a -= tls_base;
		*v = a;
		return 0;
	}
	case R_GLOBAL_INDEX_LEB: case R_GLOBAL_INDEX_I32:
		if (t.g) {
			if (t.g->out < 0) {
				err("%s: undefined global: %s", o->name, t.g->name);
				return -1;
			}
			*v = (uint64_t)t.g->out;
		} else {
			*v = (uint64_t)t.o->gl[t.idx].out;
		}
		(void)imm_globals;
		return 0;
	case R_TAG_INDEX_LEB:
		if (t.g || !t.o || t.idx >= t.o->ntag || t.o->tag[t.idx].out < 0) {
			err("%s: undefined tag: %s", o->name, t.g ? t.g->name : "?");
			return -1;
		}
		*v = (uint64_t)t.o->tag[t.idx].out;
		return 0;
	case R_TABLE_NUMBER_LEB:
		*v = 0;
		return 0;
	case R_FUNCTION_OFFSET_I32: case R_FUNCTION_OFFSET_I64: case R_SECTION_OFFSET_I32:
		*v = 0;
		return 0;
	}
	err("%s: relocation type %u is not supported", o->name, r->type);
	return -1;
}

static int apply(obj *o, irel *r, uint32_t n, uint8_t *base, uint32_t base_off)
{
	for (uint32_t i = 0; i < n; i++) {
		uint64_t v;
		uint8_t *p = base + (r[i].off - base_off);

		if (rel_value(o, &r[i], &v, NULL))
			return -1;
		switch (r[i].type) {
		case R_FUNCTION_INDEX_LEB: case R_TYPE_INDEX_LEB: case R_GLOBAL_INDEX_LEB: case R_TABLE_NUMBER_LEB:
		case R_MEMORY_ADDR_LEB: case R_TAG_INDEX_LEB:
			pad_uleb(p, v, 5);
			break;
		case R_MEMORY_ADDR_LEB64:
			pad_uleb(p, v, 10);
			break;
		case R_TABLE_INDEX_SLEB: case R_MEMORY_ADDR_SLEB: case R_MEMORY_ADDR_TLS_SLEB:
			pad_sleb(p, (int64_t)(int32_t)v, 5);
			break;
		case R_TABLE_INDEX_SLEB64: case R_MEMORY_ADDR_SLEB64: case R_MEMORY_ADDR_TLS_SLEB64:
			pad_sleb(p, (int64_t)v, 10);
			break;
		case R_TABLE_INDEX_I32: case R_MEMORY_ADDR_I32: case R_FUNCTION_INDEX_I32: case R_GLOBAL_INDEX_I32:
		case R_FUNCTION_OFFSET_I32: case R_SECTION_OFFSET_I32: {
			uint32_t w = (uint32_t)v;

			memcpy(p, &w, 4);
			break;
		}
		default:
			memcpy(p, &v, 8);
			break;
		}
	}
	return 0;
}

typedef struct oexp {
	const char *name;
	uint8_t kind;
	uint32_t idx;
} oexp;

static int write_module(void)
{
	obuf o = { 0 };
	oexp *ex = NULL;
	uint32_t nex = 0, cex = 0;
	gsym **fimps = NULL, **gimps = NULL;
	uint32_t nfi = 0, cfi = 0, ngi = 0, cgi = 0;
	struct ofn { obj *o; uint32_t f; gsym *stub; uint32_t type; } *ofns = NULL;
	uint32_t nof = 0, cof = 0;
	struct oglob { uint8_t type, mut; span init; uint64_t val; bool has_val; } *ogl = NULL;
	uint32_t nog = 0, cog = 0;
	int32_t ctors = -1;
	uint32_t *otags = NULL, notag = 0, cotag = 0;
	uint64_t mem;
	static const uint8_t t_void[] = { 0, 0 };

	/* undefined live symbols: imports, weak stubs, synthetics */
	for (uint32_t i = 0; i < ngall; i++) {
		gsym *g = gall[i];

		if (!g->live || g->state == G_DEFINED)
			continue;
		if (g->kind == K_FUNCTION || g->kind == 255) {
			if (!strcmp(g->name, "__wasm_call_ctors")) {
				g->kind = K_FUNCTION;
				g->state = G_SYNTH;
				continue;
			}
			if (!g->uo) {
				if (g->required)
					err("undefined symbol: %s (required by a run-time function)", g->name);
				else
					err("undefined symbol: %s", g->name);
				continue;
			}
			{
				isym *y = &g->uo->sym[g->usi];
				iimp *m = y->kind == K_FUNCTION && y->index < g->uo->nfimp ? &g->uo->imp[g->uo->fimp[y->index]] : NULL;
				bool allowed = g->stub || opt.allow_undefined;

				if (!allowed && m) {
					gsym *fs = gfind((const char *)m->field.p, m->field.n, false);

					allowed = fs && fs->stub;
				}
				if (y->kind != K_FUNCTION) {
					/* data: resolved as an address (synthetic) or an error later */
					continue;
				}
				if (allowed) {
					GROW(fimps, nfi, cfi);
					fimps[nfi++] = g;
				} else if (!g->strong_ref) {
					g->state = G_SYNTH;	/* an undefined weak function: a stub that traps */
				} else {
					err("%s: undefined symbol: %s", g->uo->name, g->name);
				}
			}
		}
	}
	/* function indices */
	for (uint32_t i = 0; i < nfi; i++)
		fimps[i]->out = (int32_t)i;
	{
		gsym *c = gfind("__wasm_call_ctors", 17, false);

		if (c && c->live && c->state == G_SYNTH) {
			GROW(ofns, nof, cof);
			ofns[nof] = (struct ofn){ NULL, 0, c, otype((span){ t_void, 2 }) };
			ctors = (int32_t)(nfi + nof++);
			c->out = ctors;
		}
	}
	for (uint32_t k = 0; k < nobjs; k++)
		for (uint32_t i = 0; i < objs[k]->nfn; i++)
			if (objs[k]->fn[i].live) {
				GROW(ofns, nof, cof);
				ofns[nof] = (struct ofn){ objs[k], i, NULL, 0 };
				objs[k]->fn[i].out = (int32_t)(nfi + nof++);
			}
	for (uint32_t i = 0; i < ngall; i++) {
		gsym *g = gall[i];

		if (g->live && g->state == G_SYNTH && g->kind == K_FUNCTION && g->out < 0 && g->uo) {
			isym *y = &g->uo->sym[g->usi];
			uint32_t ty = y->index < g->uo->nfimp ? g->uo->imp[g->uo->fimp[y->index]].type : 0;

			GROW(ofns, nof, cof);
			ofns[nof] = (struct ofn){ NULL, 0, g, otype(g->uo->types[ty]) };
			g->out = (int32_t)(nfi + nof++);
		}
	}
	/* tags: the objects' live ones, in order (no imports) */
	for (uint32_t k = 0; k < nobjs; k++)
		for (uint32_t i = 0; i < objs[k]->ntag; i++)
			if (objs[k]->tag[i].live) {
				GROW(otags, notag, cotag);
				otags[notag] = otype(objs[k]->types[objs[k]->tag[i].type]);
				objs[k]->tag[i].out = (int32_t)notag++;
			}
	/* globals: __stack_pointer, then the objects' */
	GROW(ogl, nog, cog);
	ogl[nog++] = (struct oglob){ 0x7e, 1, { 0 }, 0, true };
	sp_global = 0;
	for (uint32_t k = 0; k < nobjs; k++)
		for (uint32_t i = 0; i < objs[k]->ngl; i++)
			if (objs[k]->gl[i].live) {
				GROW(ogl, nog, cog);
				ogl[nog] = (struct oglob){ objs[k]->gl[i].type, objs[k]->gl[i].mut, objs[k]->gl[i].init, 0, false };
				objs[k]->gl[i].out = (int32_t)(ngi + nog++);
			}
	for (uint32_t i = 0; i < ngall; i++) {
		gsym *g = gall[i];

		if (!g->live || g->state == G_DEFINED || g->kind != K_GLOBAL)
			continue;
		if (!strcmp(g->name, "__stack_pointer"))
			g->out = (int32_t)ngi + sp_global;
		else if (!strcmp(g->name, "__tls_base") || !strcmp(g->name, "__tls_size") || !strcmp(g->name, "__tls_align") ||
			 !strcmp(g->name, "__memory_base") || !strcmp(g->name, "__table_base")) {
			GROW(ogl, nog, cog);
			ogl[nog] = (struct oglob){ 0x7e, 0, { 0 }, 0, true };
			g->out = (int32_t)(ngi + nog++);
		} else {
			err("undefined global: %s", g->name);
		}
	}
	(void)gimps, (void)cgi;

	/* data layout */
	{
		uint64_t p = opt.stack_first ? 0 : opt.global_base;

		if (opt.stack_first) {
			stack_low = 0;
			stack_high = (opt.stack_size + 15) & ~(uint64_t)15;
			p = stack_high;
		}
		/* output segments */
		for (uint32_t k = 0; k < nobjs; k++)
			for (uint32_t i = 0; i < objs[k]->nseg; i++) {
				iseg *s = &objs[k]->seg[i];
				char buf[256];
				const char *nm;
				uint32_t j;

				if (!s->live)
					continue;
				nm = oseg_name(s->name, buf, sizeof buf);
				for (j = 0; j < nosegs; j++)
					if (!strcmp(osegs[j].name, nm))
						break;
				if (j == nosegs) {
					osegs = realloc(osegs, (nosegs + 1) * sizeof(oseg));
					osegs[nosegs] = (oseg){ strdup(nm), 0, 0, 0, !strcmp(nm, ".tdata") || !strcmp(nm, ".tbss"),
								!strcmp(nm, ".bss") || !strcmp(nm, ".tbss"), seg_order(nm) };
					nosegs++;
				}
				s->oseg = (int32_t)j;
				if (s->p2align > osegs[j].p2align)
					osegs[j].p2align = s->p2align;
			}
		/* stable order: TLS, .rodata, .data, others, .bss */
		{
			uint32_t *perm = malloc((nosegs + 1) * sizeof(uint32_t)), *inv = malloc((nosegs + 1) * sizeof(uint32_t)), n = 0;
			oseg *ns = malloc((nosegs + 1) * sizeof(oseg));

			for (int ord = 0; ord <= 4; ord++)
				for (uint32_t j = 0; j < nosegs; j++)
					if (osegs[j].order == ord)
						perm[n++] = j;
			for (uint32_t j = 0; j < nosegs; j++) {
				ns[j] = osegs[perm[j]];
				inv[perm[j]] = j;
			}
			free(osegs);
			osegs = ns;
			for (uint32_t k = 0; k < nobjs; k++)
				for (uint32_t i = 0; i < objs[k]->nseg; i++)
					if (objs[k]->seg[i].oseg >= 0)
						objs[k]->seg[i].oseg = (int32_t)inv[objs[k]->seg[i].oseg];
			free(perm);
			free(inv);
		}
		for (uint32_t j = 0; j < nosegs; j++) {
			uint64_t a = (uint64_t)1 << osegs[j].p2align;

			p = (p + a - 1) & ~(a - 1);
			osegs[j].addr = p;
			for (uint32_t k = 0; k < nobjs; k++)
				for (uint32_t i = 0; i < objs[k]->nseg; i++) {
					iseg *s = &objs[k]->seg[i];
					uint64_t b = (uint64_t)1 << s->p2align;

					if (s->oseg != (int32_t)j)
						continue;
					p = (p + b - 1) & ~(b - 1);
					s->addr = p;
					p += s->data.n;
				}
			osegs[j].size = p - osegs[j].addr;
			if (osegs[j].tls) {
				if (!tls_size)
					tls_base = osegs[j].addr;
				tls_size = p - tls_base;
				if (((uint64_t)1 << osegs[j].p2align) > tls_align)
					tls_align = (uint64_t)1 << osegs[j].p2align;
			}
		}
		data_end = p;
		if (!opt.stack_first) {
			p = (p + 15) & ~(uint64_t)15;
			stack_low = p;
			p += opt.stack_size;
			stack_high = p;
		}
		heap_base = (p + 15) & ~(uint64_t)15;
		if (opt.initial_memory)
			mem = opt.initial_memory;
		else
			mem = (heap_base + opt.initial_heap + 65535) & ~(uint64_t)65535;
		mem_pages = mem / 65536;
		max_pages = opt.no_growable ? mem_pages : opt.max_memory ? opt.max_memory / 65536 : 0;
		heap_end = opt.no_growable ? mem : (max_pages ? max_pages * 65536 : 0);
		ogl[sp_global].val = stack_high;
	}
	/* synthetic globals' values */
	for (uint32_t i = 0; i < ngall; i++) {
		gsym *g = gall[i];

		if (!g->live || g->state == G_DEFINED || g->kind != K_GLOBAL || g->out < 0 || (uint32_t)g->out == (uint32_t)sp_global)
			continue;
		ogl[g->out].val = !strcmp(g->name, "__tls_base") ? tls_base : !strcmp(g->name, "__tls_size") ? tls_size
				: !strcmp(g->name, "__tls_align") ? tls_align : !strcmp(g->name, "__table_base") ? opt.table_base : 0;
	}
	/* exports */
	GROW(ex, nex, cex);
	ex[nex++] = (oexp){ "memory", 2, 0 };
	for (int pass = 0; pass < 2; pass++)
		for (uint32_t i = 0; i < (pass ? opt.nexpi : opt.nexp); i++) {
			const char *nm = pass ? opt.exports_ifdef[i] : opt.exports[i];
			gsym *g = gfind(nm, strlen(nm), false);
			tgt t;

			if (!g || (g->state != G_DEFINED && g->state != G_SYNTH)) {
				if (!pass)
					err("symbol not found: %s", nm);
				continue;
			}
			for (uint32_t j = 0; j < nex; j++)
				if (!strcmp(ex[j].name, nm))
					goto next_export;
			if (g->state == G_SYNTH) {
				if (g->kind == K_FUNCTION && g->out >= 0) {
					GROW(ex, nex, cex);
					ex[nex++] = (oexp){ g->name, 0, (uint32_t)g->out };
				}
				continue;
			}
			t = resolve(g->o, g->si);
			GROW(ex, nex, cex);
			if (t.kind == K_FUNCTION)
				ex[nex++] = (oexp){ g->name, 0, (uint32_t)t.o->fn[t.idx].out };
			else if (t.kind == K_GLOBAL)
				ex[nex++] = (oexp){ g->name, 3, (uint32_t)t.o->gl[t.idx].out };
			else if (t.kind == K_TAG)
				ex[nex++] = (oexp){ g->name, 4, (uint32_t)t.o->tag[t.idx].out };
			else if (t.kind == K_DATA) {
				/* a data symbol: an immutable global holding its address */
				GROW(ogl, nog, cog);
				ogl[nog] = (struct oglob){ 0x7e, 0, { 0 }, t.o->seg[t.y->seg].addr + t.y->off, true };
				ex[nex++] = (oexp){ g->name, 3, ngi + nog++ };
			}
		next_export:;
		}
	for (uint32_t i = 0; i < ngall; i++) {
		gsym *g = gall[i];

		/* __start_X / __stop_X exported if asked: synthetic data */
		(void)g;
	}
	if (opt.export_table) {
		GROW(ex, nex, cex);
		ex[nex++] = (oexp){ "__indirect_function_table", 1, 0 };
	}
	/* synthetic data symbols asked for as exports (__start_em_js ...) */
	for (uint32_t i = 0; i < opt.nexpi; i++) {
		gsym *g = gfind(opt.exports_ifdef[i], strlen(opt.exports_ifdef[i]), false);
		uint64_t a;

		if (g && g->state == G_UNDEF && synth_data(g, &a)) {
			GROW(ogl, nog, cog);
			ogl[nog] = (struct oglob){ 0x7e, 0, { 0 }, a, true };
			GROW(ex, nex, cex);
			ex[nex++] = (oexp){ g->name, 3, ngi + nog++ };
		}
	}
	if (nerr)
		return -1;

	/* ---- the module ---- */
	{
		obuf code = { 0 }, data = { 0 }, ty = { 0 };
		size_t at;

		/* the code first: relocations assign table slots and types */
		ouleb(&code, nof);
		for (uint32_t i = 0; i < nof; i++) {
			struct ofn *f = &ofns[i];
			obuf b = { 0 };

			if (f->stub && f->stub->out == ctors) {
				/* __wasm_call_ctors: the init functions by priority (stable) */
				ob8(&b, 0);
				for (uint32_t prio = 0, more = 1; more; prio++) {
					uint32_t nextp = UINT32_MAX;

					more = 0;
					for (uint32_t k = 0; k < nobjs; k++)
						for (uint32_t j = 0; j < objs[k]->ninit; j++) {
							uint32_t pr = objs[k]->init[j].prio;

							if (pr == prio) {
								tgt t = resolve(objs[k], objs[k]->init[j].sym);

								if (t.o && t.kind == K_FUNCTION) {
									ob8(&b, 0x10);
									ouleb(&b, (uint64_t)t.o->fn[t.idx].out);
								}
							} else if (pr > prio && pr < nextp)
								nextp = pr;
						}
					if (nextp != UINT32_MAX) {
						more = 1;
						prio = nextp - 1;
					}
				}
				ob8(&b, 0x0b);
			} else if (f->stub) {
				ob8(&b, 0);
				ob8(&b, 0x00);	/* unreachable */
				ob8(&b, 0x0b);
			} else {
				ifunc *fn = &f->o->fn[f->f];

				ob_put(&b, fn->body.p, fn->body.n);
				if (apply(f->o, f->o->crel + fn->rel0, fn->nrel, b.p, fn->off))
					return -1;
			}
			ouleb(&code, b.n);
			ob_put(&code, b.p, b.n);
			free(b.p);
		}
		/* data */
		{
			uint32_t nd = 0;

			for (uint32_t j = 0; j < nosegs; j++)
				nd += !osegs[j].bss;
			ouleb(&data, nd);
			for (uint32_t j = 0; j < nosegs; j++) {
				uint8_t *buf;

				if (osegs[j].bss)
					continue;
				buf = calloc(osegs[j].size + 1, 1);
				for (uint32_t k = 0; k < nobjs; k++)
					for (uint32_t i = 0; i < objs[k]->nseg; i++) {
						iseg *s = &objs[k]->seg[i];

						if (s->oseg != (int32_t)j)
							continue;
						memcpy(buf + (s->addr - osegs[j].addr), s->data.p, s->data.n);
						if (apply(objs[k], objs[k]->drel + s->rel0, s->nrel, buf + (s->addr - osegs[j].addr), s->off))
							return -1;
					}
				ob8(&data, 0);
				ob8(&data, 0x42);
				osleb(&data, (int64_t)osegs[j].addr);
				ob8(&data, 0x0b);
				ouleb(&data, osegs[j].size);
				ob_put(&data, buf, osegs[j].size);
				free(buf);
			}
		}
		/* function types: imports and functions (after the code: call_indirect types came first there) */
		{
			uint32_t *ftypes = malloc((nfi + nof + 1) * sizeof(uint32_t));

			for (uint32_t i = 0; i < nfi; i++) {
				isym *y = &fimps[i]->uo->sym[fimps[i]->usi];

				ftypes[i] = otype(fimps[i]->uo->types[fimps[i]->uo->imp[fimps[i]->uo->fimp[y->index]].type]);
			}
			for (uint32_t i = 0; i < nof; i++)
				ftypes[nfi + i] = ofns[i].stub ? ofns[i].type : otype(ofns[i].o->types[ofns[i].o->fn[ofns[i].f].type]);

			ob_put(&o, "\0asm\1\0\0\0", 8);
			at = sec_begin(&o, 1, NULL);
			ouleb(&o, notypes);
			for (uint32_t i = 0; i < notypes; i++) {
				ob8(&o, 0x60);
				ob_put(&o, otypes[i].p, otypes[i].n);
			}
			sec_end(&o, at);
			if (nfi) {
				at = sec_begin(&o, 2, NULL);
				ouleb(&o, nfi);
				for (uint32_t i = 0; i < nfi; i++) {
					gsym *g = fimps[i];
					isym *y = &g->uo->sym[g->usi];
					iimp *m = &g->uo->imp[g->uo->fimp[y->index]];
					bool stubname = g->stub && !(m->module.n == 3 && !memcmp(m->module.p, "env", 3));

					(void)stubname;
					ouleb(&o, m->module.n);
					ob_put(&o, m->module.p, m->module.n);
					ouleb(&o, m->field.n);
					ob_put(&o, m->field.p, m->field.n);
					ob8(&o, 0);
					ouleb(&o, ftypes[i]);
				}
				sec_end(&o, at);
			}
			at = sec_begin(&o, 3, NULL);
			ouleb(&o, nof);
			for (uint32_t i = 0; i < nof; i++)
				ouleb(&o, ftypes[nfi + i]);
			sec_end(&o, at);
			free(ftypes);
		}
		/* table */
		at = sec_begin(&o, 4, NULL);
		ouleb(&o, 1);
		ob8(&o, 0x70);
		ob8(&o, opt.growable_table ? 0x04 : 0x05);
		ouleb(&o, opt.table_base + nslots);
		if (!opt.growable_table)
			ouleb(&o, opt.table_base + nslots);
		sec_end(&o, at);
		/* memory */
		at = sec_begin(&o, 5, NULL);
		ouleb(&o, 1);
		ob8(&o, max_pages ? 0x05 : 0x04);
		ouleb(&o, mem_pages);
		if (max_pages)
			ouleb(&o, max_pages);
		sec_end(&o, at);
		/* tags */
		if (notag) {
			at = sec_begin(&o, 13, NULL);
			ouleb(&o, notag);
			for (uint32_t i = 0; i < notag; i++) {
				ob8(&o, 0);
				ouleb(&o, otags[i]);
			}
			sec_end(&o, at);
		}
		/* globals */
		at = sec_begin(&o, 6, NULL);
		ouleb(&o, nog);
		for (uint32_t i = 0; i < nog; i++) {
			ob8(&o, ogl[i].type);
			ob8(&o, ogl[i].mut);
			if (ogl[i].has_val) {
				ob8(&o, ogl[i].type == 0x7f ? 0x41 : 0x42);
				osleb(&o, (int64_t)ogl[i].val);
				ob8(&o, 0x0b);
			} else {
				ob_put(&o, ogl[i].init.p, ogl[i].init.n);
			}
		}
		sec_end(&o, at);
		/* exports */
		at = sec_begin(&o, 7, NULL);
		ouleb(&o, nex);
		for (uint32_t i = 0; i < nex; i++) {
			ostr(&o, ex[i].name);
			ob8(&o, ex[i].kind);
			ouleb(&o, ex[i].idx);
		}
		sec_end(&o, at);
		/* the table's functions */
		if (nslots) {
			at = sec_begin(&o, 9, NULL);
			ouleb(&o, 1);
			ob8(&o, 0);
			ob8(&o, 0x42);
			osleb(&o, (int64_t)opt.table_base);
			ob8(&o, 0x0b);
			ouleb(&o, nslots);
			for (uint32_t i = 0; i < nslots; i++)
				ouleb(&o, slot_func[i]);
			sec_end(&o, at);
		}
		at = sec_begin(&o, 10, NULL);
		ob_put(&o, code.p, code.n);
		sec_end(&o, at);
		at = sec_begin(&o, 11, NULL);
		ob_put(&o, data.p, data.n);
		sec_end(&o, at);
		/* target_features: what the inputs use */
		{
			span fe[64];
			uint32_t nfe = 0;

			for (uint32_t k = 0; k < nobjs; k++) {
				rd r = { objs[k]->features.p, objs[k]->features.p + objs[k]->features.n, false };
				uint32_t c = objs[k]->features.n ? (uint32_t)ruleb(&r) : 0;

				for (uint32_t i = 0; i < c && !r.bad; i++) {
					uint8_t pre = r8(&r);
					span nm = rname(&r);
					uint32_t j;

					if (pre != '+')
						continue;
					for (j = 0; j < nfe; j++)
						if (span_eq(fe[j], nm))
							break;
					if (j == nfe && nfe < 64)
						fe[nfe++] = nm;
				}
			}
			for (uint32_t i = 0; i < opt.nfeat; i++) {
				span nm = { (const uint8_t *)opt.features[i], (uint32_t)strlen(opt.features[i]) };
				uint32_t j;

				for (j = 0; j < nfe; j++)
					if (span_eq(fe[j], nm))
						break;
				if (j == nfe && nfe < 64)
					fe[nfe++] = nm;
			}
			if (nfe) {
				at = sec_begin(&o, 0, "target_features");
				ouleb(&o, nfe);
				for (uint32_t i = 0; i < nfe; i++) {
					ob8(&o, '+');
					ouleb(&o, fe[i].n);
					ob_put(&o, fe[i].p, fe[i].n);
				}
				sec_end(&o, at);
			}
		}
		free(code.p);
		free(data.p);
		free(ty.p);
	}
	{
		char tmp[4200];
		int fd;

		snprintf(tmp, sizeof tmp, "%s.tmp%d", opt.out, (int)getpid());
		fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0755);
		if (fd < 0 || write(fd, o.p, o.n) != (ssize_t)o.n || close(fd) || rename(tmp, opt.out)) {
			err("cannot write %s: %s", opt.out, strerror(errno));
			unlink(tmp);
			return -1;
		}
	}
	free(o.p);
	return 0;
}

/* ---------------- the driver ---------------- */

static void add_list(const char ***v, uint32_t *n, uint32_t *c, const char *s)
{
	GROW(*v, *n, *c);
	(*v)[(*n)++] = s;
}

int wasm_ld_main(int argc, char **argv)
{
	const char **inputs = NULL;
	uint32_t nin = 0, cin = 0;

	opt.out = "a.out";
	/* options first: -L applies to every -l */
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];

#define VAL(flag) (!strcmp(a, flag) && i + 1 < argc ? argv[++i] : !strncmp(a, flag "=", strlen(flag) + 1) ? a + strlen(flag) + 1 : NULL)
		const char *v;

		if (!strcmp(a, "-o") && i + 1 < argc)
			opt.out = argv[++i];
		else if (!strncmp(a, "-L", 2))
			add_list(&opt.libdirs, &opt.nlib, &opt.clib, a[2] ? a + 2 : argv[++i]);
		else if (!strcmp(a, "-z") && i + 1 < argc) {
			v = argv[++i];
			if (!strncmp(v, "stack-size=", 11))
				opt.stack_size = num(v + 11);
			else
				err("unknown -z option %s", v);
		} else if ((v = VAL("--export")))
			add_list(&opt.exports, &opt.nexp, &opt.cexp, v);
		else if ((v = VAL("--export-if-defined")))
			add_list(&opt.exports_ifdef, &opt.nexpi, &opt.cexpi, v);
		else if ((v = VAL("--initial-heap")))
			opt.initial_heap = num(v);
		else if ((v = VAL("--initial-memory")))
			opt.initial_memory = num(v);
		else if ((v = VAL("--max-memory")))
			opt.max_memory = num(v);
		else if ((v = VAL("--global-base")))
			opt.global_base = num(v);
		else if ((v = VAL("--table-base")))
			opt.table_base = num(v);
		else if ((v = VAL("--extra-features"))) {
			char *d = strdup(v), *t;

			for (t = strtok(d, ","); t; t = strtok(NULL, ","))
				add_list(&opt.features, &opt.nfeat, &opt.cfeat, t);
		} else if ((v = VAL("--entry")))
			opt.entry = v;
		else if (!strcmp(a, "--export-table"))
			opt.export_table = true;
		else if (!strcmp(a, "--stack-first"))
			opt.stack_first = true;
		else if (!strcmp(a, "--no-growable-memory"))
			opt.no_growable = true;
		else if (!strcmp(a, "--growable-table"))
			opt.growable_table = true;
		else if (!strcmp(a, "--allow-undefined"))
			opt.allow_undefined = true;
		else if (!strcmp(a, "--no-entry"))
			opt.no_entry = true;
		else if (!strcmp(a, "--gc-sections"))
			opt.gc = true;
		else if (!strcmp(a, "--no-gc-sections"))
			opt.gc = false;
		else if (!strcmp(a, "-mllvm") && i + 1 < argc)
			i++;
		else if (!strcmp(a, "-m") && i + 1 < argc)
			i++;
		else if (!strcmp(a, "-mwasm64") || !strcmp(a, "-Bstatic") || !strcmp(a, "--strip-debug") ||
			 !strcmp(a, "--strip-all") || !strcmp(a, "-Bdynamic"))
			;
		else if (!strncmp(a, "-l", 2) || a[0] != '-')
			add_list(&inputs, &nin, &cin, a);
		else
			err("unknown option: %s", a);
#undef VAL
	}
	if (nerr)
		return 1;
	if (opt.entry && strcmp(opt.entry, "_start"))
		err("--entry is not supported (emscripten uses --no-entry)");
	/* exports are references: they pull archive members (--export-if-defined too, as wasm-ld) */
	for (int pass = 0; pass < 2; pass++)
		for (uint32_t k = 0; k < (pass ? opt.nexpi : opt.nexp); k++) {
			const char *nm = pass ? opt.exports_ifdef[k] : opt.exports[k];

			if (strcmp(nm, "__wasm_call_ctors"))
				gname(nm)->strong_ref = gname(nm)->referenced = true;
		}
	for (uint32_t i = 0; i < nin && !nerr; i++) {
		if (!strncmp(inputs[i], "-l", 2))
			load_lib(inputs[i] + 2);
		else
			load_path(inputs[i]);
	}
	/* stub dependencies of referenced stub symbols, and exports, until nothing more loads */
	for (bool again = true; again && !nerr;) {
		uint32_t before = nobjs;

		again = false;
		for (uint32_t i = 0; i < ngall; i++) {
			gsym *g = gall[i];

			if (g->state == G_LAZY && g->strong_ref) {
				g->state = G_UNDEF;
				load_member(g->ar, g->member);
			}
			if (g->stub && g->referenced && g->deps) {
				char *d = g->deps;

				while (*d) {
					char *c = strchr(d, ',');
					size_t len = c ? (size_t)(c - d) : strlen(d);
					gsym *x = gfind(d, len, true);

					if (!x->strong_ref) {
						x->strong_ref = x->referenced = true;
						if (x->state == G_LAZY) {
							x->state = G_UNDEF;
							load_member(x->ar, x->member);
						}
					}
					d += len + (c ? 1 : 0);
				}
			}
		}
		/* a stub's dependencies named by import field (fd_write for __wasi_fd_write) */
		for (uint32_t k = 0; k < nobjs; k++)
			for (uint32_t i = 0; i < objs[k]->nsym; i++) {
				isym *y = &objs[k]->sym[i];

				if (y->kind == K_FUNCTION && (y->flags & F_UNDEFINED) && y->index < objs[k]->nfimp) {
					iimp *m = &objs[k]->imp[objs[k]->fimp[y->index]];
					gsym *fs = gfind((const char *)m->field.p, m->field.n, false);

					if (fs && fs->stub && !fs->referenced) {
						fs->referenced = true;
						again = true;
					}
				}
			}
		if (nobjs != before)
			again = true;
	}
	if (nerr)
		return 1;
	gc();
	/* stub symbols used only by import field name: their dependencies are roots too */
	for (uint32_t k = 0; k < nobjs; k++)
		for (uint32_t i = 0; i < objs[k]->nsym; i++) {
			isym *y = &objs[k]->sym[i];

			if (y->kind == K_FUNCTION && (y->flags & F_UNDEFINED) && y->index < objs[k]->nfimp && y->g && y->g->live) {
				iimp *m = &objs[k]->imp[objs[k]->fimp[y->index]];
				gsym *fs = gfind((const char *)m->field.p, m->field.n, false);

				if (fs && fs != y->g && fs->stub && !fs->live && fs->deps) {
					mark_gsym(fs);
					fs->live = true;
				}
			}
		}
	gc();
	if (nerr)
		return 1;
	return write_module() || nerr ? 1 : 0;
}
