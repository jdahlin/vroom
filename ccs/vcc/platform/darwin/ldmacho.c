/*
 * vld for macOS host programs: arm64 Mach-O MH_OBJECT files -> an MH_EXECUTE
 * that dyld runs, without ld64.  ld_main() hands a link here when its first
 * input is Mach-O.
 *
 * Scope is what vcc's Mach-O objects (macho.c) and the kernel's host
 * programs need:
 *   - inputs: MH_OBJECT arm64 objects; sections __TEXT,* and __DATA,*
 *     (zerofill included); debug and __LD sections are dropped;
 *   - relocations: UNSIGNED (64-bit pointers), BRANCH26, PAGE21,
 *     PAGEOFF12, GOT_LOAD_PAGE21/PAGEOFF12, ADDEND; always extern (macho.c
 *     relocates against symbols);
 *   - libraries: libSystem only (-lc -lm -lpthread -ldl -lSystem are all
 *     libSystem); undefined symbols must be exported by the SDK's
 *     libSystem.B.tbd (arm64e targets, which arm64 links against).
 *
 * Output, in ld64's arrangement: __PAGEZERO, __TEXT (headers, __text,
 * __stubs, constant sections), __DATA_CONST (__got), __DATA, __LINKEDIT
 * (chained fixups in DYLD_CHAINED_PTR_64_OFFSET, an exports trie with
 * __mh_execute_header, symbol table, indirect symbols, ad-hoc linker
 * signature: SHA-256 per 4 KiB page as codesign -s - would).  Calls to
 * imports go through 12-byte stubs that load the import's __got slot; GOT
 * loads of symbols defined in the link are relaxed to ADRP+ADD, as ld64
 * does, so local data needs no rebase.
 */
#include "../../ld/ld.h"
#include "../../lib/ldhash.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NONE 0xffffffffu
#define BASE 0x100000000ull
#define PAGE 0x4000ull

enum {
	MH_MAGIC_64 = 0xfeedfacf, CPU_ARM64 = 0x0100000c, MH_OBJECT = 1, MH_EXECUTE = 2,
	LC_SEGMENT_64 = 0x19, LC_SYMTAB = 2, LC_DYSYMTAB = 0xb, LC_LOAD_DYLIB = 0xc, LC_LOAD_DYLINKER = 0xe,
	LC_UUID = 0x1b, LC_CODE_SIGNATURE = 0x1d, LC_BUILD_VERSION = 0x32, LC_MAIN = 0x80000028,
	LC_DYLD_EXPORTS_TRIE = 0x80000033, LC_DYLD_CHAINED_FIXUPS = 0x80000034, LC_SOURCE_VERSION = 0x2a,
	N_EXT = 1, N_TYPE = 0xe, N_UNDF = 0, N_ABS = 2, N_SECT = 0xe, N_STAB = 0xe0, N_PEXT = 0x10,
	N_WEAK_DEF = 0x80,
	S_ZEROFILL = 1, S_NON_LAZY_SYMBOL_POINTERS = 6, S_SYMBOL_STUBS = 8, S_GB_ZEROFILL = 0xc,
	S_THREAD_LOCAL_REGULAR = 0x11, S_THREAD_LOCAL_ZEROFILL = 0x12, S_THREAD_LOCAL_VARIABLES = 0x13,
	S_ATTR_PURE_INSTRUCTIONS = 0x80000000, S_ATTR_SOME_INSTRUCTIONS = 0x400, S_ATTR_DEBUG = 0x02000000,
	R_UNSIGNED = 0, R_SUBTRACTOR, R_BRANCH26, R_PAGE21, R_PAGEOFF12, R_GOT_PAGE21, R_GOT_PAGEOFF12,
	R_POINTER_TO_GOT, R_TLVP_PAGE21, R_TLVP_PAGEOFF12, R_ADDEND,
};

typedef struct {
	char sectname[16], segname[16];
	uint64_t addr, size;
	uint32_t offset, align, reloff, nreloc, flags, r1, r2, r3;
} Sect;
typedef struct { uint32_t strx; uint8_t type, sect; uint16_t desc; uint64_t value; } Nlist;
typedef struct { int32_t addr; uint32_t info; } Reloc;

static FILE *errf;
static int nerr;

static void error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (nerr++ < 20) {
		fputs("vld: error: ", errf);
		vfprintf(errf, fmt, ap);
		fputc('\n', errf);
	}
	va_end(ap);
}

static void *xmalloc(size_t n)
{
	void *p = calloc(1, n ? n : 1);
	if (!p) {
		fputs("vld: out of memory\n", errf);
		exit(1);
	}
	return p;
}
#define VGROW(v, n, cap) do { if ((n) >= (cap)) { (cap) = (cap) ? 2 * (cap) : 16; \
	(v) = realloc((v), (cap) * sizeof(*(v))); if (!(v)) { fputs("vld: out of memory\n", errf); exit(1); } } } while (0)

static inline uint32_t rd32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline void wr32(void *p, uint32_t v) { memcpy(p, &v, 4); }
static inline void wr64(void *p, uint64_t v) { memcpy(p, &v, 8); }
static inline void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static inline void be64(uint8_t *p, uint64_t v) { be32(p, v >> 32); be32(p + 4, (uint32_t)v); }
static inline uint64_t alignup(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

/* ---------------- inputs ---------------- */

typedef struct {
	const char *path;
	uint8_t *buf;
	size_t size;
	Sect *sect;
	uint32_t nsect;
	uint32_t *osec;			/* per section: output section or NONE (dropped) */
	uint64_t *off;			/* per section: offset in its output section */
	const Nlist *sym;
	uint32_t nsym;
	const char *str;
	uint32_t strsize;
	uint32_t *gsym;			/* per symbol: global id (external ones) or NONE */
	uint32_t minos;
} Obj;

static Obj *objs;
static uint32_t nobjs;

/* output sections */
enum { SEG_TEXT, SEG_DATA_CONST, SEG_DATA, SEG_LINKEDIT, NSEG };
typedef struct {
	char seg[16], name[16];
	int segi;
	uint32_t flags, align;		/* align: bytes */
	uint64_t size, addr, fileoff;
	bool zerofill;
	uint8_t *data;			/* output image pointer once laid out */
} OSec;
static OSec *osecs;
static uint32_t nosecs, cap_osecs;
static uint32_t os_text = NONE, os_stubs = NONE, os_got = NONE;

static uint32_t osec_get(const char *seg, const char *name, uint32_t flags)
{
	for (uint32_t i = 0; i < nosecs; i++)
		if (!strncmp(osecs[i].seg, seg, 16) && !strncmp(osecs[i].name, name, 16))
			return i;
	VGROW(osecs, nosecs, cap_osecs);
	OSec *o = &osecs[nosecs];
	memset(o, 0, sizeof *o);
	strncpy(o->seg, seg, 16);
	strncpy(o->name, name, 16);
	o->flags = flags;
	o->align = 1;
	uint32_t t = flags & 0xff;
	o->zerofill = t == S_ZEROFILL || t == S_GB_ZEROFILL;
	o->segi = !strncmp(seg, "__TEXT", 16) ? SEG_TEXT : !strncmp(seg, "__DATA_CONST", 16) ? SEG_DATA_CONST : SEG_DATA;
	return nosecs++;
}

/* globals */
typedef struct {
	const char *name;
	uint32_t obj, sym;		/* definition, or NONE */
	bool weak, common;
	uint64_t csize;			/* common: size */
	uint32_t calign;
	uint32_t import;		/* index in imports, or NONE */
	uint32_t got, stub;		/* slots, or NONE */
	uint32_t ref_obj;		/* first referencing object */
	uint64_t addr;			/* final address of a definition */
} G;
static G *gs;
static uint32_t ngs, cap_gs;
static uint32_t *gh;			/* open addressing: id + 1 */
static uint32_t ghmask;

static uint32_t strhash(const char *s)
{
	uint32_t h = 2166136261u;
	for (; *s; s++)
		h = (h ^ (uint8_t)*s) * 16777619u;
	return h;
}
static uint32_t gfind(const char *name, bool add)
{
	uint32_t h = strhash(name) & ghmask;
	for (;; h = (h + 1) & ghmask) {
		uint32_t v = gh[h];
		if (!v)
			break;
		if (!strcmp(gs[v - 1].name, name))
			return v - 1;
	}
	if (!add)
		return NONE;
	if (2 * (ngs + 1) > ghmask) {	/* grow and rehash */
		uint32_t nm = 2 * (ghmask + 1) - 1, *nh = xmalloc((nm + 1) * sizeof(uint32_t));
		for (uint32_t i = 0; i < ngs; i++) {
			uint32_t k = strhash(gs[i].name) & nm;
			while (nh[k])
				k = (k + 1) & nm;
			nh[k] = i + 1;
		}
		free(gh);
		gh = nh;
		ghmask = nm;
		return gfind(name, add);
	}
	VGROW(gs, ngs, cap_gs);
	G *g = &gs[ngs];
	memset(g, 0, sizeof *g);
	g->name = name;
	g->obj = g->sym = g->import = g->got = g->stub = g->ref_obj = NONE;
	gh[h] = ngs + 1;
	return ngs++;
}

/* ---------------- libSystem exports (SDK .tbd) ---------------- */

static const char *sdk_root(void)
{
	const char *s = getenv("SDKROOT");
	return s && *s ? s : "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";
}

static char *tbd;			/* libSystem.B.tbd, NUL-terminated */
static uint32_t *ex_hash, ex_mask;	/* exported names: offsets into tbd + 1 */
static uint32_t sdk_version;		/* SDKSettings "Version", xxxx.yy.zz */

static char *read_whole(const char *path, size_t *n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return NULL;
	struct stat st;
	if (fstat(fd, &st)) {
		close(fd);
		return NULL;
	}
	char *b = xmalloc((size_t)st.st_size + 1);
	size_t got = 0;
	while (got < (size_t)st.st_size) {
		ssize_t r = read(fd, b + got, (size_t)st.st_size - got);
		if (r <= 0)
			break;
		got += (size_t)r;
	}
	close(fd);
	b[got] = 0;
	if (n)
		*n = got;
	return b;
}

static uint32_t parse_version(const char *s)
{
	uint32_t x = (uint32_t)strtoul(s, (char **)&s, 10) << 16;
	if (*s == '.')
		x |= ((uint32_t)strtoul(s + 1, (char **)&s, 10) & 0xff) << 8;
	if (*s == '.')
		x |= (uint32_t)strtoul(s + 1, (char **)&s, 10) & 0xff;
	return x;
}

/*
 * The symbols of every `symbols:`, `weak-symbols:` and
 * `thread-local-symbols:` list whose `targets:` include an arm64 macOS
 * target, over all documents (libSystem and its re-exported libraries).
 * Names are NUL-terminated in place.
 */
static bool load_libsystem(void)
{
	if (tbd)
		return true;
	char path[1024];
	snprintf(path, sizeof path, "%s/usr/lib/libSystem.B.tbd", sdk_root());
	size_t n;
	if (!(tbd = read_whole(path, &n))) {
		error("cannot read %s: %s", path, strerror(errno));
		return false;
	}
	snprintf(path, sizeof path, "%s/SDKSettings.json", sdk_root());
	char *js = read_whole(path, NULL);
	const char *v = js ? strstr(js, "\"Version\":\"") : NULL;
	sdk_version = v ? parse_version(v + 11) : 0;
	free(js);
	ex_mask = (1u << 15) - 1;
	ex_hash = xmalloc((ex_mask + 1) * sizeof(uint32_t));
	uint32_t count = 0;
	bool arm = false;
	char *p = tbd, *end = tbd + n;
	while (p < end) {
		char *line = p, *nl = memchr(p, '\n', (size_t)(end - p));
		char *le = nl ? nl : end;
		p = nl ? nl + 1 : end;
		char *k = line;
		while (k < le && (*k == ' ' || *k == '-'))
			k++;
		bool syms = false;
		if (!strncmp(k, "targets:", 8)) {
			/* the list may continue on following lines up to ']' */
			char *close = memchr(k, ']', (size_t)(end - k));
			arm = false;
			for (char *q = k; close && q < close; q++)
				if (!strncmp(q, "arm64-macos", 11) || !strncmp(q, "arm64e-macos", 12))
					arm = true;
			if (close)
				p = memchr(close, '\n', (size_t)(end - close)) ? (char *)memchr(close, '\n', (size_t)(end - close)) + 1 : end;
			continue;
		}
		if (!strncmp(k, "symbols:", 8) || !strncmp(k, "weak-symbols:", 13) || !strncmp(k, "thread-local-symbols:", 21))
			syms = true;
		if (!syms)
			continue;
		char *q = memchr(k, '[', (size_t)(le - k));
		if (!q)
			continue;
		q++;
		for (;;) {
			while (q < end && (*q == ' ' || *q == '\n' || *q == ','))
				q++;
			if (q >= end || *q == ']')
				break;
			char *b = q, *e;
			if (*q == '\'') {
				b = ++q;
				e = memchr(q, '\'', (size_t)(end - q));
				if (!e)
					break;
				q = e + 1;
			} else {
				while (q < end && *q != ',' && *q != ']' && *q != ' ' && *q != '\n')
					q++;
				e = q;
			}
			char save = *e;
			*e = 0;
			if (arm && e > b) {
				if (2 * (count + 1) > ex_mask) {
					uint32_t nm = 2 * (ex_mask + 1) - 1, *nh = xmalloc((nm + 1) * sizeof(uint32_t));
					for (uint32_t i = 0; i <= ex_mask; i++)
						if (ex_hash[i]) {
							uint32_t h = strhash(tbd + ex_hash[i] - 1) & nm;
							while (nh[h])
								h = (h + 1) & nm;
							nh[h] = ex_hash[i];
						}
					free(ex_hash);
					ex_hash = nh;
					ex_mask = nm;
				}
				uint32_t h = strhash(b) & ex_mask;
				while (ex_hash[h] && strcmp(tbd + ex_hash[h] - 1, b))
					h = (h + 1) & ex_mask;
				if (!ex_hash[h]) {
					ex_hash[h] = (uint32_t)(b - tbd) + 1;
					count++;
				}
			}
			if (save == ']') {
				break;
			}
			if (e == q && save != ']')
				q = e + 1;
		}
		p = memchr(q, '\n', (size_t)(end - q)) ? (char *)memchr(q, '\n', (size_t)(end - q)) + 1 : end;
	}
	return true;
}

static bool libsystem_exports(const char *name)
{
	uint32_t h = strhash(name) & ex_mask;
	for (; ex_hash[h]; h = (h + 1) & ex_mask)
		if (!strcmp(tbd + ex_hash[h] - 1, name))
			return true;
	return false;
}

/* ---------------- reading objects ---------------- */

static bool read_obj(Obj *o)
{
	const uint8_t *b = o->buf;
	if (o->size < 32 || rd32(b) != MH_MAGIC_64 || rd32(b + 4) != CPU_ARM64 || rd32(b + 12) != MH_OBJECT) {
		error("%s: not an arm64 Mach-O object", o->path);
		return false;
	}
	uint32_t ncmds = rd32(b + 16), sizeofcmds = rd32(b + 20);
	if (32 + (uint64_t)sizeofcmds > o->size) {
		error("%s: truncated load commands", o->path);
		return false;
	}
	const uint8_t *lc = b + 32;
	for (uint32_t i = 0; i < ncmds; i++) {
		uint32_t cmd = rd32(lc), sz = rd32(lc + 4);
		if (sz < 8 || lc + sz > b + 32 + sizeofcmds) {
			error("%s: bad load command", o->path);
			return false;
		}
		if (cmd == LC_SEGMENT_64) {
			uint32_t ns = rd32(lc + 64);
			if (72 + (uint64_t)ns * 80 > sz) {
				error("%s: bad segment", o->path);
				return false;
			}
			o->sect = xmalloc(ns * sizeof(Sect));
			memcpy(o->sect, lc + 72, ns * sizeof(Sect));
			o->nsect = ns;
		} else if (cmd == LC_SYMTAB) {
			uint32_t symoff = rd32(lc + 8), nsyms = rd32(lc + 12), stroff = rd32(lc + 16), strsize = rd32(lc + 20);
			if ((uint64_t)symoff + (uint64_t)nsyms * 16 > o->size || (uint64_t)stroff + strsize > o->size) {
				error("%s: bad symbol table", o->path);
				return false;
			}
			o->sym = (const Nlist *)(const void *)(b + symoff);
			o->nsym = nsyms;
			o->str = (const char *)b + stroff;
			o->strsize = strsize;
		} else if (cmd == LC_BUILD_VERSION) {
			o->minos = rd32(lc + 12);
		}
		lc += sz;
	}
	for (uint32_t s = 0; s < o->nsect; s++) {
		Sect *x = &o->sect[s];
		uint32_t t = x->flags & 0xff;
		bool zf = t == S_ZEROFILL || t == S_GB_ZEROFILL;
		if (!zf && (uint64_t)x->offset + x->size > o->size) {
			error("%s: section %.16s out of range", o->path, x->sectname);
			return false;
		}
		if ((uint64_t)x->reloff + (uint64_t)x->nreloc * 8 > o->size) {
			error("%s: relocations of %.16s out of range", o->path, x->sectname);
			return false;
		}
	}
	return true;
}

static const char *sym_name(const Obj *o, uint32_t i)
{
	uint32_t x = o->sym[i].strx;
	return x < o->strsize ? o->str + x : "";
}

/* ---------------- the link ---------------- */

typedef struct { uint64_t addr; uint64_t val; } Fixup;	/* val: bind import + 1 << 63, or rebase target */
static Fixup *fixups;
static uint32_t nfix, cap_fix;
static uint32_t *imports;		/* global ids */
static uint32_t nimports, cap_imports;
static uint32_t ngot, nstub;
static uint32_t *got_g, *stub_g;	/* slot -> global */

static void add_fixup(uint64_t addr, uint64_t val)
{
	VGROW(fixups, nfix, cap_fix);
	fixups[nfix++] = (Fixup){addr, val};
}

static uint32_t import_of(uint32_t g)
{
	if (gs[g].import == NONE) {
		VGROW(imports, nimports, cap_imports);
		gs[g].import = nimports;
		imports[nimports++] = g;
	}
	return gs[g].import;
}
static uint32_t got_of(uint32_t g)
{
	if (gs[g].got == NONE) {
		gs[g].got = ngot++;
		got_g = realloc(got_g, ngot * sizeof(uint32_t));
		got_g[ngot - 1] = g;
	}
	return gs[g].got;
}
static uint32_t stub_of(uint32_t g)
{
	if (gs[g].stub == NONE) {
		got_of(g);
		gs[g].stub = nstub++;
		stub_g = realloc(stub_g, nstub * sizeof(uint32_t));
		stub_g[nstub - 1] = g;
	}
	return gs[g].stub;
}

/* address of symbol i of object o (defined), or of the global it resolves to */
typedef struct { bool import; uint32_t g; uint64_t addr; } Tgt;

static uint64_t local_addr(const Obj *o, uint32_t i)
{
	const Nlist *n = &o->sym[i];
	if ((n->type & N_TYPE) == N_ABS)
		return n->value;
	uint32_t s = n->sect - 1u;
	if (s >= o->nsect || o->osec[s] == NONE)
		return 0;
	return osecs[o->osec[s]].addr + o->off[s] + (n->value - o->sect[s].addr);
}

static bool target(const Obj *o, uint32_t symi, Tgt *t)
{
	if (symi >= o->nsym) {
		error("%s: bad relocation symbol", o->path);
		return false;
	}
	t->import = false;
	t->g = o->gsym[symi];
	if (t->g == NONE) {
		t->addr = local_addr(o, symi);
		return true;
	}
	G *g = &gs[t->g];
	if (g->import != NONE) {
		t->import = true;
		return true;
	}
	t->addr = g->addr;
	return true;
}

static int ldst_scale(uint32_t ins)
{
	/* load/store (unsigned immediate): size in 31:30; 128-bit vector when opc<1> and size 00 */
	if ((ins & 0x3b000000) != 0x39000000)
		return 0;
	int sz = (int)(ins >> 30);
	if (sz == 0 && (ins & 0x04800000) == 0x04800000)
		return 4;
	return sz;
}

static void put_page21(uint8_t *loc, uint64_t S, uint64_t P, const char *path)
{
	int64_t d = (int64_t)((S & ~0xfffull) - (P & ~0xfffull)) >> 12;
	if (d < -(1 << 20) || d >= (1 << 20))
		error("%s: ADRP out of range", path);
	uint32_t ins = rd32(loc) & ~(0x60000000u | 0x00ffffe0u);
	wr32(loc, ins | (uint32_t)(d & 3) << 29 | (uint32_t)((d >> 2) & 0x7ffff) << 5);
}
static void put_lo12(uint8_t *loc, uint64_t S)
{
	uint32_t ins = rd32(loc);
	int sh = ldst_scale(ins);
	uint32_t imm = (uint32_t)(S & 0xfff) >> sh;
	wr32(loc, (ins & ~(0xfffu << 10)) | imm << 10);
}

/* pass over all relocations: kind 0 scans (stubs, GOT slots, fixups), 1 applies */
static void relocs(int pass, uint8_t *img)
{
	for (uint32_t oi = 0; oi < nobjs; oi++) {
		Obj *o = &objs[oi];
		for (uint32_t s = 0; s < o->nsect; s++) {
			Sect *x = &o->sect[s];
			if (o->osec[s] == NONE || !x->nreloc)
				continue;
			OSec *os = &osecs[o->osec[s]];
			const Reloc *r = (const Reloc *)(const void *)(o->buf + x->reloff);
			uint64_t secaddr = os->addr + o->off[s];
			uint8_t *sb = pass ? img + os->fileoff + o->off[s] : NULL;
			int64_t addend = 0;
			for (uint32_t k = 0; k < x->nreloc; k++) {
				uint32_t a = (uint32_t)r[k].addr, info = r[k].info;
				uint32_t symnum = info & 0xffffff, pcrel = (info >> 24) & 1, len = (info >> 25) & 3;
				uint32_t ext = (info >> 27) & 1, type = info >> 28;
				if ((int32_t)a < 0 || a + (1u << len) > x->size) {
					error("%s: relocation outside %.16s", o->path, x->sectname);
					continue;
				}
				if (type == R_ADDEND) {
					addend = (int32_t)(symnum << 8) >> 8;
					continue;
				}
				if (!ext) {
					error("%s: section-relative relocations are not supported", o->path);
					continue;
				}
				Tgt t;
				if (!target(o, symnum, &t))
					continue;
				uint64_t P = secaddr + a;
				uint8_t *loc = sb ? sb + a : NULL;
				int64_t A = addend;
				addend = 0;
				switch (type) {
				case R_UNSIGNED: {
					if (len != 3 || pcrel) {
						error("%s: UNSIGNED relocation of %u bytes is not supported", o->path, 1u << len);
						break;
					}
					int64_t inplace = 0;
					memcpy(&inplace, o->buf + x->offset + a, 8);
					if (!pass) {
						if (t.import)
							add_fixup(P, 1ull << 63 | (uint64_t)gs[t.g].import | (uint64_t)(uint8_t)inplace << 32);
						else
							add_fixup(P, t.addr + (uint64_t)inplace);
						if (t.import && (inplace < 0 || inplace > 255))
							error("%s: pointer to %s with addend %lld", o->path, gs[t.g].name, (long long)inplace);
					}
					/* the fixup chain is written over the slot later */
					break;
				}
				case R_BRANCH26: {
					if (!pass) {
						if (t.import)
							stub_of(t.g);
						break;
					}
					uint64_t S = t.import ? osecs[os_stubs].addr + 12ull * gs[t.g].stub : t.addr;
					int64_t d = (int64_t)(S + (uint64_t)A - P);
					if ((d & 3) || d < -(1ll << 27) || d >= (1ll << 27))
						error("%s: branch out of range", o->path);
					wr32(loc, (rd32(loc) & 0xfc000000u) | ((uint32_t)(d >> 2) & 0x03ffffffu));
					break;
				}
				case R_PAGE21:
				case R_PAGEOFF12:
					if (t.import) {
						if (!pass)
							error("%s: direct reference to imported %s", o->path, gs[t.g].name);
						break;
					}
					if (!pass)
						break;
					if (type == R_PAGE21)
						put_page21(loc, t.addr + (uint64_t)A, P, o->path);
					else
						put_lo12(loc, t.addr + (uint64_t)A);
					break;
				case R_GOT_PAGE21:
				case R_GOT_PAGEOFF12: {
					if (!pass) {
						if (t.import)
							import_of(t.g), got_of(t.g);
						break;
					}
					if (!t.import) {
						/* relax: ADRP to the symbol, LDR -> ADD (ld64) */
						if (type == R_GOT_PAGE21) {
							put_page21(loc, t.addr, P, o->path);
						} else {
							uint32_t ins = rd32(loc);
							if ((ins & 0xffc00000u) != 0xf9400000u) {
								error("%s: GOT_LOAD_PAGEOFF12 not on a 64-bit LDR", o->path);
								break;
							}
							uint32_t rt = ins & 0x1f, rn = (ins >> 5) & 0x1f;
							wr32(loc, 0x91000000u | rt | rn << 5 | (uint32_t)(t.addr & 0xfff) << 10);
						}
						break;
					}
					uint64_t S = osecs[os_got].addr + 8ull * gs[t.g].got;
					if (type == R_GOT_PAGE21)
						put_page21(loc, S, P, o->path);
					else
						put_lo12(loc, S);
					break;
				}
				default:
					if (!pass)
						error("%s: unsupported relocation type %u in %.16s", o->path, type, x->sectname);
				}
			}
		}
	}
}

/* ---------------- output helpers ---------------- */

typedef struct { uint8_t *p; size_t n, cap; } Buf;
static void bput(Buf *b, const void *d, size_t n)
{
	if (b->n + n > b->cap) {
		b->cap = (b->n + n) * 2 + 256;
		b->p = realloc(b->p, b->cap);
		if (!b->p) {
			fputs("vld: out of memory\n", errf);
			exit(1);
		}
	}
	memcpy(b->p + b->n, d, n);
	b->n += n;
}
static void b32(Buf *b, uint32_t v) { bput(b, &v, 4); }
static void b64(Buf *b, uint64_t v) { bput(b, &v, 8); }
static void b16(Buf *b, uint16_t v) { bput(b, &v, 2); }
static void bpad(Buf *b, size_t a) { static const uint8_t z[16]; while (b->n % a) bput(b, z, 1); }

static int cmp_fix(const void *a, const void *b)
{
	uint64_t x = ((const Fixup *)a)->addr, y = ((const Fixup *)b)->addr;
	return (x > y) - (x < y);
}
static int cmp_name(const void *a, const void *b)
{
	return strcmp(gs[*(const uint32_t *)a].name, gs[*(const uint32_t *)b].name);
}

typedef struct { const char *name; uint64_t addr; uint8_t type, sect; uint16_t desc; } OutSym;

/* ---------------- driver ---------------- */

static void reset(void)
{
	for (uint32_t i = 0; i < nobjs; i++) {
		free(objs[i].buf);
		free(objs[i].sect);
		free(objs[i].osec);
		free(objs[i].off);
		free(objs[i].gsym);
	}
	free(objs);
	free(osecs);
	free(gs);
	free(gh);
	free(fixups);
	free(imports);
	free(got_g);
	free(stub_g);
	objs = NULL;
	osecs = NULL;
	gs = NULL;
	gh = NULL;
	fixups = NULL;
	imports = got_g = stub_g = NULL;
	nobjs = nosecs = cap_osecs = ngs = cap_gs = nfix = cap_fix = nimports = cap_imports = ngot = nstub = 0;
	os_text = os_stubs = os_got = NONE;
	nerr = 0;
}

static uint32_t section_index_of(uint32_t osi, const uint32_t *order, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (order[i] == osi)
			return i + 1;
	return 0;
}

static int link_macho(int argc, char **argv, int dirfd, FILE *err);

/* one link at a time: the state above is global (vccd calls this from its workers) */
int ld_macho(int argc, char **argv, int dirfd, FILE *err)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_lock(&lock);
	int rc = link_macho(argc, argv, dirfd, err);
	pthread_mutex_unlock(&lock);
	return rc;
}

static int link_macho(int argc, char **argv, int dirfd, FILE *err)
{
	errf = err ? err : stderr;
	reset();
	const char *out = "a.out";
	const char **inputs = xmalloc((size_t)argc * sizeof(char *));
	uint32_t ninputs = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-o") && i + 1 < argc) { out = argv[++i]; continue; }
		if (!strncmp(a, "-l", 2)) {
			const char *l = a + 2;
			if (strcmp(l, "c") && strcmp(l, "m") && strcmp(l, "pthread") && strcmp(l, "dl") && strcmp(l, "System"))
				error("library -l%s is not supported (only libSystem)", l);
			continue;
		}
		if (!strcmp(a, "-arch") && i + 1 < argc) {
			if (strcmp(argv[++i], "arm64"))
				error("-arch %s is not supported", argv[i]);
			continue;
		}
		if (!strncmp(a, "-L", 2)) { if (!a[2]) i++; continue; }
		if (!strcmp(a, "--vroom-stubs"))
			continue;
		if (!strcmp(a, "-dead_strip") || !strcmp(a, "-no_dead_strip") || !strcmp(a, "-pie") ||
		    !strcmp(a, "-dynamic") || !strcmp(a, "-execute") || !strcmp(a, "-demangle"))
			continue;
		if (!strcmp(a, "-platform_version") && i + 3 < argc) { i += 3; continue; }
		if (!strcmp(a, "-syslibroot") && i + 1 < argc) { i++; continue; }
		if (a[0] == '-' && a[1]) {
			error("unknown argument: %s", a);
			continue;
		}
		inputs[ninputs++] = a;
	}
	if (!ninputs)
		error("no input files");
	if (nerr) {
		free(inputs);
		return 1;
	}

	/* read inputs */
	objs = xmalloc(ninputs * sizeof(Obj));
	for (uint32_t i = 0; i < ninputs; i++) {
		Obj *o = &objs[nobjs++];
		o->path = inputs[i];
		int fd = openat(dirfd, inputs[i], O_RDONLY | O_CLOEXEC);
		struct stat st;
		if (fd < 0 || fstat(fd, &st)) {
			error("cannot open %s: %s", inputs[i], strerror(errno));
			if (fd >= 0)
				close(fd);
			continue;
		}
		o->size = (size_t)st.st_size;
		o->buf = xmalloc(o->size + 8);
		size_t got = 0;
		while (got < o->size) {
			ssize_t r = read(fd, o->buf + got, o->size - got);
			if (r <= 0)
				break;
			got += (size_t)r;
		}
		close(fd);
		if (got != o->size || !read_obj(o))
			error("%s: cannot read", inputs[i]);
	}
	if (nerr)
		goto fail;

	/* output sections: __text and __stubs first in __TEXT, __got alone in __DATA_CONST */
	os_text = osec_get("__TEXT", "__text", S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS);
	os_stubs = osec_get("__TEXT", "__stubs", S_SYMBOL_STUBS | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS);
	osecs[os_stubs].align = 4;
	os_got = osec_get("__DATA_CONST", "__got", S_NON_LAZY_SYMBOL_POINTERS);
	osecs[os_got].align = 8;
	uint32_t minos = 0;
	for (uint32_t oi = 0; oi < nobjs; oi++) {
		Obj *o = &objs[oi];
		if (o->minos > minos)
			minos = o->minos;
		o->osec = xmalloc((o->nsect + 1) * sizeof(uint32_t));
		o->off = xmalloc((o->nsect + 1) * sizeof(uint64_t));
		for (uint32_t s = 0; s < o->nsect; s++) {
			Sect *x = &o->sect[s];
			uint32_t t = x->flags & 0xff;
			o->osec[s] = NONE;
			if ((x->flags & S_ATTR_DEBUG) || !strncmp(x->segname, "__LD", 16) || !strncmp(x->segname, "__DWARF", 16))
				continue;
			if (t == S_THREAD_LOCAL_REGULAR || t == S_THREAD_LOCAL_ZEROFILL || t == S_THREAD_LOCAL_VARIABLES) {
				error("%s: thread-local section %.16s is not supported", o->path, x->sectname);
				continue;
			}
			if (strncmp(x->segname, "__TEXT", 16) && strncmp(x->segname, "__DATA", 16)) {
				error("%s: section %.16s,%.16s is not supported", o->path, x->segname, x->sectname);
				continue;
			}
			uint32_t osi = osec_get(x->segname, x->sectname, x->flags);
			OSec *os = &osecs[osi];
			uint32_t al = x->align < 16 ? 1u << x->align : 1u << 15;
			if (al > os->align)
				os->align = al;
			os->size = alignup(os->size, al);
			o->osec[s] = osi;
			o->off[s] = os->size;
			os->size += x->size;
		}
	}

	/* symbols */
	ghmask = 1023;
	gh = xmalloc((ghmask + 1) * sizeof(uint32_t));
	for (uint32_t oi = 0; oi < nobjs; oi++) {
		Obj *o = &objs[oi];
		o->gsym = xmalloc((o->nsym + 1) * sizeof(uint32_t));
		for (uint32_t i = 0; i < o->nsym; i++) {
			const Nlist *n = &o->sym[i];
			o->gsym[i] = NONE;
			if ((n->type & N_STAB) || !(n->type & N_EXT))
				continue;
			uint32_t g = gfind(sym_name(o, i), true);
			o->gsym[i] = g;
			G *x = &gs[g];
			uint8_t ty = n->type & N_TYPE;
			if (ty == N_UNDF && n->value) {	/* common */
				if (x->obj == NONE) {
					x->common = true;
					if (n->value > x->csize)
						x->csize = n->value;
					uint32_t al = 1u << ((n->desc >> 8) & 0xf);
					if (al > x->calign)
						x->calign = al;
				}
				continue;
			}
			if (ty == N_UNDF) {
				if (x->ref_obj == NONE)
					x->ref_obj = oi;
				continue;
			}
			bool weak = n->desc & N_WEAK_DEF;
			if (x->obj != NONE) {
				if (x->weak && !weak) {
					x->obj = oi, x->sym = i, x->weak = false;
				} else if (!weak && !x->weak) {
					error("duplicate symbol: %s\n>>> defined in %s\n>>> defined in %s", x->name,
					      objs[x->obj].path, o->path);
				}
				continue;
			}
			x->obj = oi;
			x->sym = i;
			x->weak = weak;
			x->common = false;
		}
	}
	/* commons into __DATA,__common */
	for (uint32_t g = 0; g < ngs; g++)
		if (gs[g].common && gs[g].obj == NONE) {
			uint32_t osi = osec_get("__DATA", "__common", S_ZEROFILL);
			OSec *os = &osecs[osi];
			uint32_t al = gs[g].calign ? gs[g].calign : 1;
			if (al > os->align)
				os->align = al;
			os->size = alignup(os->size, al);
			gs[g].addr = os->size;	/* offset until layout */
			os->size += gs[g].csize;
		}
	/* undefined -> imports from libSystem */
	bool need_sys = false;
	for (uint32_t g = 0; g < ngs; g++)
		if (gs[g].obj == NONE && !gs[g].common)
			need_sys = true;
	if (need_sys && !load_libsystem())
		goto fail;
	if (!sdk_version)
		load_libsystem();
	for (uint32_t g = 0; g < ngs; g++) {
		G *x = &gs[g];
		if (x->obj != NONE || x->common)
			continue;
		if (!libsystem_exports(x->name)) {
			error("undefined symbol: %s\n>>> referenced by %s", x->name,
			      x->ref_obj != NONE ? objs[x->ref_obj].path : "?");
			continue;
		}
		import_of(g);
	}
	uint32_t gmain = gfind("_main", false);
	if (gmain == NONE || gs[gmain].obj == NONE)
		error("undefined symbol: _main");
	if (nerr)
		goto fail;

	/* scan relocations: stubs, GOT slots, pointer fixups (addresses come later) */
	/* (pass 0 needs no addresses except for fixups, which are redone after layout) */
	relocs(0, NULL);
	if (nerr)
		goto fail;
	osecs[os_stubs].size = 12ull * nstub;
	osecs[os_got].size = 8ull * ngot;

	/* output order: per segment, __text/__stubs first, zerofill last */
	uint32_t *order = xmalloc((nosecs + 1) * sizeof(uint32_t)), nord = 0;
	for (int seg = 0; seg < SEG_LINKEDIT; seg++)
		for (int zf = 0; zf < 2; zf++)
			for (uint32_t i = 0; i < nosecs; i++)
				if (osecs[i].segi == seg && osecs[i].zerofill == zf && osecs[i].size)
					order[nord++] = i;

	/* load commands: sizes first */
	const char *dylinker = "/usr/lib/dyld", *libsys = "/usr/lib/libSystem.B.dylib";
	uint32_t nsegsecs[NSEG] = {0};
	for (uint32_t i = 0; i < nord; i++)
		nsegsecs[osecs[order[i]].segi]++;
	bool segpresent[NSEG] = { true, nsegsecs[SEG_DATA_CONST] > 0, nsegsecs[SEG_DATA] > 0, true };
	uint32_t nsegs = 1;	/* __PAGEZERO */
	uint32_t sizeofcmds = 72;
	for (int s = 0; s < NSEG; s++)
		if (segpresent[s]) {
			nsegs++;
			sizeofcmds += 72 + 80 * nsegsecs[s];
		}
	uint32_t dylinker_sz = (uint32_t)alignup(12 + strlen(dylinker) + 1, 8);
	uint32_t libsys_sz = (uint32_t)alignup(24 + strlen(libsys) + 1, 8);
	sizeofcmds += 16 + 16 + 24 + 80 + dylinker_sz + 24 + 24 + 16 + 24 + libsys_sz + 16;
	/* chained fixups, exports trie, symtab, dysymtab, dylinker, uuid, build version,
	 * source version, main, load dylib, code signature */
	uint32_t ncmds = nsegs + 11;

	/* layout */
	uint64_t addr = BASE + 32 + sizeofcmds, segstart[NSEG], segend[NSEG], segfile[NSEG], segfilesz[NSEG];
	uint64_t fileoff = 0;
	segstart[SEG_TEXT] = BASE;
	segfile[SEG_TEXT] = 0;
	for (int seg = 0; seg < SEG_LINKEDIT; seg++) {
		if (seg > SEG_TEXT) {
			segstart[seg] = addr = alignup(addr, PAGE);
			segfile[seg] = fileoff = alignup(fileoff, PAGE);
		}
		uint64_t fend = segstart[seg];
		for (uint32_t i = 0; i < nord; i++) {
			OSec *os = &osecs[order[i]];
			if (os->segi != seg)
				continue;
			addr = alignup(addr, os->align);
			os->addr = addr;
			os->fileoff = os->zerofill ? 0 : segfile[seg] + (addr - segstart[seg]);
			addr += os->size;
			if (!os->zerofill)
				fend = addr;
		}
		segend[seg] = alignup(addr, PAGE);
		if (seg == SEG_TEXT) {
			segend[seg] = alignup(addr, PAGE);
			segfilesz[seg] = segend[seg] - segstart[seg];
		} else {
			segfilesz[seg] = segpresent[seg] && fend > segstart[seg] ? alignup(fend - segstart[seg], PAGE) : 0;
		}
		if (!segfilesz[seg] && seg > SEG_TEXT)
			segfile[seg] = 0;
		fileoff = segfile[seg] + segfilesz[seg];
		if (seg == SEG_TEXT)
			fileoff = segfilesz[seg];
		if (!segpresent[seg]) {
			segend[seg] = segstart[seg];
			addr = segstart[seg];
		}
	}
	segstart[SEG_LINKEDIT] = alignup(addr, PAGE);
	for (int seg = SEG_DATA_CONST; seg < SEG_LINKEDIT; seg++)
		if (segpresent[seg] && segfilesz[seg])
			fileoff = segfile[seg] + segfilesz[seg];
	segfile[SEG_LINKEDIT] = alignup(fileoff, PAGE);

	/* final symbol addresses */
	for (uint32_t g = 0; g < ngs; g++) {
		G *x = &gs[g];
		if (x->obj != NONE)
			x->addr = local_addr(&objs[x->obj], x->sym);
		else if (x->common)
			x->addr += osecs[osec_get("__DATA", "__common", S_ZEROFILL)].addr;
	}
	/* fixups again, now with addresses: GOT slots and data pointers */
	nfix = 0;
	{
		uint32_t keep_ngot = ngot, keep_nstub = nstub;
		relocs(0, NULL);	/* slots are idempotent; fixups recorded with final addresses */
		if (ngot != keep_ngot || nstub != keep_nstub)
			error("internal: slot count changed");
	}
	for (uint32_t k = 0; k < ngot; k++) {
		uint32_t g = got_g[k];
		uint64_t at = osecs[os_got].addr + 8ull * k;
		if (gs[g].import != NONE)
			add_fixup(at, 1ull << 63 | gs[g].import);
		else
			add_fixup(at, gs[g].addr);
	}
	qsort(fixups, nfix, sizeof(Fixup), cmp_fix);
	for (uint32_t k = 0; k < nfix; k++) {
		if (fixups[k].addr & 7)
			error("pointer fixup at 0x%llx is not 8-byte aligned", (unsigned long long)fixups[k].addr);
		if (!(fixups[k].val >> 63) && (fixups[k].val < BASE || fixups[k].val - BASE >= (1ull << 36)))
			error("pointer fixup target 0x%llx out of range", (unsigned long long)fixups[k].val);
	}
	if (nerr)
		goto fail;

	/* ---- __LINKEDIT ---- */
	Buf le = {0};
	/* chained fixups */
	uint64_t cf_off = 0;
	{
		Buf cf = {0};
		uint32_t segidx[NSEG + 1];	/* LC_SEGMENT ordinal of each of our segments */
		uint32_t si = 1;
		for (int s = 0; s < NSEG; s++)
			segidx[s] = segpresent[s] ? si++ : NONE;
		/* header (28 bytes), padded to 32 */
		for (int i = 0; i < 8; i++)
			b32(&cf, 0);
		uint32_t starts_off = (uint32_t)cf.n;
		uint32_t seg_count = nsegs;
		b32(&cf, seg_count);
		size_t seginfo_at = cf.n;
		for (uint32_t i = 0; i < seg_count; i++)
			b32(&cf, 0);
		for (int s = SEG_DATA_CONST; s <= SEG_DATA; s++) {
			if (!segpresent[s])
				continue;
			uint64_t lo = segstart[s], hi = segend[s];
			uint32_t first = 0;
			while (first < nfix && fixups[first].addr < lo)
				first++;
			if (first == nfix || fixups[first].addr >= hi)
				continue;
			bpad(&cf, 4);
			uint32_t info_off = (uint32_t)cf.n - starts_off;
			memcpy(cf.p + seginfo_at + 4 * segidx[s], &info_off, 4);
			uint16_t pages = (uint16_t)((hi - lo) / PAGE);
			uint32_t sz = 22 + 2 * pages;
			b32(&cf, sz);
			b16(&cf, (uint16_t)PAGE);
			b16(&cf, 6);	/* DYLD_CHAINED_PTR_64_OFFSET */
			b64(&cf, lo - BASE);
			b32(&cf, 0);
			b16(&cf, pages);
			for (uint16_t p = 0; p < pages; p++) {
				uint16_t st = 0xffff;
				for (uint32_t k = first; k < nfix && fixups[k].addr < hi; k++)
					if ((fixups[k].addr - lo) / PAGE == p) {
						st = (uint16_t)((fixups[k].addr - lo) % PAGE);
						break;
					}
				b16(&cf, st);
			}
		}
		bpad(&cf, 4);
		uint32_t imports_off = (uint32_t)cf.n;
		Buf names = {0};
		bput(&names, "", 1);
		for (uint32_t i = 0; i < nimports; i++) {
			uint32_t no = (uint32_t)names.n;
			bput(&names, gs[imports[i]].name, strlen(gs[imports[i]].name) + 1);
			b32(&cf, 1u | no << 9);	/* lib ordinal 1 (libSystem), not weak */
		}
		uint32_t symbols_off = (uint32_t)cf.n;
		bput(&cf, names.p, names.n);
		free(names.p);
		bpad(&cf, 8);
		uint32_t hdr[7] = { 0, starts_off, imports_off, symbols_off, nimports, 1, 0 };
		memcpy(cf.p, hdr, sizeof hdr);
		cf_off = le.n;
		bput(&le, cf.p, cf.n);
		free(cf.p);
	}
	uint64_t cf_size = le.n - cf_off;
	/* exports trie: __mh_execute_header at offset 0 */
	uint64_t ex_off = le.n;
	{
		static const char nm[] = "__mh_execute_header";
		uint8_t root[2 + sizeof nm + 1];
		root[0] = 0;
		root[1] = 1;
		memcpy(root + 2, nm, sizeof nm);
		root[2 + sizeof nm] = (uint8_t)(2 + sizeof nm + 1);	/* child offset (uleb, < 128) */
		bput(&le, root, sizeof root);
		uint8_t child[4] = { 2, 0, 0, 0 };	/* terminal: flags 0, address 0; no children */
		bput(&le, child, sizeof child);
		bpad(&le, 8);
	}
	uint64_t ex_size = le.n - ex_off;

	/* symbol table: locals, external definitions, imports */
	OutSym *syms = xmalloc((ngs + 1 + 64) * sizeof(OutSym));
	uint32_t nsyms = 0, nlocal = 0, nextdef = 0, nundef = 0;
	uint32_t *sorted = xmalloc((ngs + 1) * sizeof(uint32_t)), ns = 0;
	for (uint32_t oi = 0; oi < nobjs; oi++) {
		Obj *o = &objs[oi];
		for (uint32_t i = 0; i < o->nsym; i++) {
			const Nlist *n = &o->sym[i];
			if ((n->type & N_STAB) || (n->type & N_EXT) || (n->type & N_TYPE) != N_SECT)
				continue;
			const char *nm = sym_name(o, i);
			if (nm[0] == 'l' || nm[0] == 'L' || !nm[0])
				continue;
			uint32_t s = n->sect - 1u;
			if (s >= o->nsect || o->osec[s] == NONE)
				continue;
			syms = realloc(syms, (nsyms + ngs + 2) * sizeof(OutSym));
			syms[nsyms++] = (OutSym){nm, local_addr(o, i), N_SECT, (uint8_t)section_index_of(o->osec[s], order, nord), 0};
			nlocal++;
		}
	}
	syms = realloc(syms, (nsyms + ngs + 2) * sizeof(OutSym));
	syms[nsyms++] = (OutSym){"__mh_execute_header", BASE, N_SECT | N_EXT, 1, 0x10};
	nextdef++;
	for (uint32_t g = 0; g < ngs; g++)
		if (gs[g].obj != NONE || gs[g].common)
			sorted[ns++] = g;
	qsort(sorted, ns, sizeof(uint32_t), cmp_name);
	for (uint32_t k = 0; k < ns; k++) {
		G *x = &gs[sorted[k]];
		uint32_t osi = NONE;
		if (x->obj != NONE) {
			const Obj *o = &objs[x->obj];
			const Nlist *n = &o->sym[x->sym];
			if ((n->type & N_TYPE) == N_SECT && n->sect - 1u < o->nsect)
				osi = o->osec[n->sect - 1u];
		} else {
			osi = osec_get("__DATA", "__common", S_ZEROFILL);
		}
		syms[nsyms++] = (OutSym){x->name, x->addr, N_SECT | N_EXT,
					 (uint8_t)(osi == NONE ? 0 : section_index_of(osi, order, nord)), 0};
		nextdef++;
	}
	/* "__mh_execute_header" sorts before user names in strcmp order only by chance: keep ld64's sort */
	{
		OutSym *e = syms + nlocal;
		for (uint32_t a = 1; a < nextdef; a++) {
			OutSym t = e[a];
			uint32_t b = a;
			while (b > 0 && strcmp(e[b - 1].name, t.name) > 0) {
				e[b] = e[b - 1];
				b--;
			}
			e[b] = t;
		}
	}
	ns = 0;
	for (uint32_t i = 0; i < nimports; i++)
		sorted[ns++] = imports[i];
	qsort(sorted, ns, sizeof(uint32_t), cmp_name);
	uint32_t undef0 = nsyms;
	for (uint32_t k = 0; k < ns; k++) {
		syms[nsyms++] = (OutSym){gs[sorted[k]].name, 0, N_UNDF | N_EXT, 0, 1u << 8};
		gs[sorted[k]].calign = nsyms - 1;	/* reused: symbol index of the import */
		nundef++;
	}
	(void)undef0;
	Buf strt = {0};
	bput(&strt, " ", 2);
	bpad(&le, 8);
	uint64_t sym_off = le.n;
	for (uint32_t i = 0; i < nsyms; i++) {
		Nlist n = { (uint32_t)strt.n, syms[i].type, syms[i].sect, syms[i].desc, syms[i].addr };
		bput(&strt, syms[i].name, strlen(syms[i].name) + 1);
		bput(&le, &n, sizeof n);
	}
	/* indirect symbols: __stubs then __got */
	uint64_t ind_off = le.n;
	for (uint32_t k = 0; k < nstub; k++)
		b32(&le, gs[stub_g[k]].calign);
	for (uint32_t k = 0; k < ngot; k++)
		b32(&le, gs[got_g[k]].import != NONE ? gs[got_g[k]].calign : 0x80000000u);
	uint32_t nind = nstub + ngot;
	bpad(&le, 8);	/* dyld wants the string pool 8-aligned */
	uint64_t str_off = le.n;
	bpad(&strt, 8);
	bput(&le, strt.p, strt.n);
	free(strt.p);
	uint64_t str_size = le.n - str_off;
	bpad(&le, 16);
	uint64_t sig_off = le.n;

	/* code signature size */
	const char *ident = strrchr(out, '/') ? strrchr(out, '/') + 1 : out;
	uint64_t code_limit = segfile[SEG_LINKEDIT] + sig_off;
	uint32_t nslots = (uint32_t)((code_limit + 4095) / 4096);
	uint32_t cd_size = 88 + (uint32_t)strlen(ident) + 1 + 32 * nslots;
	uint32_t sig_size = 20 + cd_size;
	uint64_t le_size = sig_off + sig_size;
	uint64_t filesize = segfile[SEG_LINKEDIT] + le_size;

	/* ---- the image ---- */
	uint8_t *img = xmalloc(filesize);
	Buf lc = {0};
	{
		uint32_t mh[8] = { MH_MAGIC_64, CPU_ARM64, 0, MH_EXECUTE, ncmds, sizeofcmds,
				   0x1 | 0x4 | 0x80 | 0x200000, 0 };	/* NOUNDEFS DYLDLINK TWOLEVEL PIE */
		bput(&lc, mh, sizeof mh);
	}
#define SEGCMD(name, vmaddr, vmsize, fo, fs, maxp, initp, nsec, fl) do { \
		char sn[16] = {0}; strncpy(sn, name, 16); \
		b32(&lc, LC_SEGMENT_64); b32(&lc, 72 + 80 * (nsec)); bput(&lc, sn, 16); \
		b64(&lc, vmaddr); b64(&lc, vmsize); b64(&lc, fo); b64(&lc, fs); \
		b32(&lc, maxp); b32(&lc, initp); b32(&lc, nsec); b32(&lc, fl); } while (0)
	SEGCMD("__PAGEZERO", 0, BASE, 0, 0, 0, 0, 0, 0);
	static const char *segnames[NSEG] = { "__TEXT", "__DATA_CONST", "__DATA", "__LINKEDIT" };
	for (int s = 0; s < SEG_LINKEDIT; s++) {
		if (!segpresent[s])
			continue;
		uint32_t prot = s == SEG_TEXT ? 5 : 3;
		SEGCMD(segnames[s], segstart[s], segend[s] - segstart[s], segfile[s], segfilesz[s], prot, prot,
		       nsegsecs[s], s == SEG_DATA_CONST ? 0x10u : 0u);
		for (uint32_t i = 0; i < nord; i++) {
			OSec *os = &osecs[order[i]];
			if (os->segi != s)
				continue;
			bput(&lc, os->name, 16);
			bput(&lc, os->seg, 16);
			b64(&lc, os->addr);
			b64(&lc, os->size);
			b32(&lc, (uint32_t)os->fileoff);
			uint32_t al = 0;
			while ((1u << al) < os->align)
				al++;
			b32(&lc, al);
			b32(&lc, 0);
			b32(&lc, 0);
			b32(&lc, os->flags);
			b32(&lc, order[i] == os_stubs ? 0 : order[i] == os_got ? nstub : 0);
			b32(&lc, order[i] == os_stubs ? 12 : 0);
			b32(&lc, 0);
		}
	}
	SEGCMD("__LINKEDIT", segstart[SEG_LINKEDIT], alignup(le_size, PAGE), segfile[SEG_LINKEDIT], le_size, 1, 1, 0, 0);
	b32(&lc, LC_DYLD_CHAINED_FIXUPS); b32(&lc, 16);
	b32(&lc, (uint32_t)(segfile[SEG_LINKEDIT] + cf_off)); b32(&lc, (uint32_t)cf_size);
	b32(&lc, LC_DYLD_EXPORTS_TRIE); b32(&lc, 16);
	b32(&lc, (uint32_t)(segfile[SEG_LINKEDIT] + ex_off)); b32(&lc, (uint32_t)ex_size);
	b32(&lc, LC_SYMTAB); b32(&lc, 24);
	b32(&lc, (uint32_t)(segfile[SEG_LINKEDIT] + sym_off)); b32(&lc, nsyms);
	b32(&lc, (uint32_t)(segfile[SEG_LINKEDIT] + str_off)); b32(&lc, (uint32_t)str_size);
	{
		uint32_t d[18] = { LC_DYSYMTAB, 80, 0, nlocal, nlocal, nextdef, nlocal + nextdef, nundef,
				   0, 0, 0, 0, 0, 0, (uint32_t)(segfile[SEG_LINKEDIT] + ind_off), nind, 0, 0 };
		bput(&lc, d, sizeof d);
		b32(&lc, 0); b32(&lc, 0);
	}
	{
		size_t at = lc.n;
		b32(&lc, LC_LOAD_DYLINKER); b32(&lc, dylinker_sz); b32(&lc, 12);
		bput(&lc, dylinker, strlen(dylinker) + 1);
		while (lc.n - at < dylinker_sz)
			bput(&lc, "", 1);
	}
	size_t uuid_at = lc.n + 8;
	b32(&lc, LC_UUID); b32(&lc, 24); b64(&lc, 0); b64(&lc, 0);
	b32(&lc, LC_BUILD_VERSION); b32(&lc, 24); b32(&lc, 1);	/* macOS */
	b32(&lc, minos ? minos : 0x000b0000); b32(&lc, sdk_version ? sdk_version : minos); b32(&lc, 0);
	b32(&lc, LC_SOURCE_VERSION); b32(&lc, 16); b64(&lc, 0);
	b32(&lc, LC_MAIN); b32(&lc, 24); b64(&lc, gs[gmain].addr - BASE); b64(&lc, 0);
	{
		size_t at = lc.n;
		b32(&lc, LC_LOAD_DYLIB); b32(&lc, libsys_sz); b32(&lc, 24); b32(&lc, 2);
		b32(&lc, parse_version("1359.40.3")); b32(&lc, 0x10000);
		bput(&lc, libsys, strlen(libsys) + 1);
		while (lc.n - at < libsys_sz)
			bput(&lc, "", 1);
	}
	b32(&lc, LC_CODE_SIGNATURE); b32(&lc, 16);
	b32(&lc, (uint32_t)(segfile[SEG_LINKEDIT] + sig_off)); b32(&lc, sig_size);
	if (lc.n != 32 + sizeofcmds)
		error("internal: load commands are %zu bytes, expected %u", lc.n, 32 + sizeofcmds);
	memcpy(img, lc.p, lc.n);
	free(lc.p);

	/* section contents */
	for (uint32_t oi = 0; oi < nobjs; oi++) {
		Obj *o = &objs[oi];
		for (uint32_t s = 0; s < o->nsect; s++) {
			if (o->osec[s] == NONE)
				continue;
			OSec *os = &osecs[o->osec[s]];
			if (os->zerofill)
				continue;
			memcpy(img + os->fileoff + o->off[s], o->buf + o->sect[s].offset, o->sect[s].size);
		}
	}
	/* stubs: adrp x16, slot@PAGE; ldr x16, [x16, slot@PAGEOFF]; br x16 */
	for (uint32_t k = 0; k < nstub; k++) {
		uint8_t *p = img + osecs[os_stubs].fileoff + 12ull * k;
		uint64_t P = osecs[os_stubs].addr + 12ull * k, S = osecs[os_got].addr + 8ull * gs[stub_g[k]].got;
		wr32(p, 0x90000010u);
		put_page21(p, S, P, "<stubs>");
		wr32(p + 4, 0xf9400210u | (uint32_t)((S & 0xfff) >> 3) << 10);
		wr32(p + 8, 0xd61f0200u);
	}
	relocs(1, img);
	/* chained fixup words */
	for (uint32_t k = 0; k < nfix; k++) {
		Fixup *f = &fixups[k];
		uint32_t osi = NONE;
		for (uint32_t i = 0; i < nord; i++) {
			OSec *os = &osecs[order[i]];
			if (!os->zerofill && f->addr >= os->addr && f->addr < os->addr + os->size)
				osi = order[i];
		}
		if (osi == NONE) {
			error("pointer fixup at 0x%llx is not in a section with contents", (unsigned long long)f->addr);
			continue;
		}
		uint64_t next = 0;
		if (k + 1 < nfix && (fixups[k + 1].addr - BASE) / PAGE == (f->addr - BASE) / PAGE) {
			uint64_t d = (fixups[k + 1].addr - f->addr) / 4;
			next = d;
		}
		uint64_t w;
		if (f->val >> 63)
			w = 1ull << 63 | next << 51 | (f->val & 0xffffff) | ((f->val >> 32) & 0xff) << 24;
		else
			w = next << 51 | ((f->val - BASE) & ((1ull << 36) - 1));
		wr64(img + osecs[osi].fileoff + (f->addr - osecs[osi].addr), w);
	}
	memcpy(img + segfile[SEG_LINKEDIT], le.p, le.n);
	free(le.p);

	/* UUID: from the content before the signature */
	{
		uint64_t h1 = ld_xxh3(img, code_limit), h2 = ld_xxh3(img + code_limit / 2, code_limit - code_limit / 2);
		uint8_t u[16];
		memcpy(u, &h1, 8);
		memcpy(u + 8, &h2, 8);
		u[6] = (u[6] & 0x0f) | 0x30;	/* name-based UUID */
		u[8] = (u[8] & 0x3f) | 0x80;
		memcpy(img + uuid_at, u, 16);
	}
	/* ad-hoc linker signature */
	{
		uint8_t *sg = img + segfile[SEG_LINKEDIT] + sig_off;
		be32(sg, 0xfade0cc0);
		be32(sg + 4, sig_size);
		be32(sg + 8, 1);
		be32(sg + 12, 0);	/* CSSLOT_CODEDIRECTORY */
		be32(sg + 16, 20);
		uint8_t *cd = sg + 20;
		uint32_t idlen = (uint32_t)strlen(ident) + 1;
		be32(cd, 0xfade0c02);
		be32(cd + 4, cd_size);
		be32(cd + 8, 0x20400);
		be32(cd + 12, 0x20002);	/* CS_ADHOC | CS_LINKER_SIGNED */
		be32(cd + 16, 88 + idlen);	/* hashOffset */
		be32(cd + 20, 88);	/* identOffset */
		be32(cd + 24, 0);	/* nSpecialSlots */
		be32(cd + 28, nslots);
		be32(cd + 32, (uint32_t)code_limit);
		cd[36] = 32;		/* hashSize */
		cd[37] = 2;		/* SHA-256 */
		cd[38] = 0;		/* platform */
		cd[39] = 12;		/* 4 KiB pages */
		be32(cd + 40, 0);	/* spare2 */
		be32(cd + 44, 0);	/* scatterOffset */
		be32(cd + 48, 0);	/* teamOffset */
		be32(cd + 52, 0);	/* spare3 */
		be64(cd + 56, 0);	/* codeLimit64 */
		be64(cd + 64, segfile[SEG_TEXT]);	/* execSegBase */
		be64(cd + 72, segfilesz[SEG_TEXT]);	/* execSegLimit */
		be64(cd + 80, 1);	/* CS_EXECSEG_MAIN_BINARY */
		memcpy(cd + 88, ident, idlen);
		uint8_t *h = cd + 88 + idlen;
		for (uint32_t p = 0; p < nslots; p++) {
			uint64_t b = (uint64_t)p * 4096, n = code_limit - b < 4096 ? code_limit - b : 4096;
			ld_sha256(img + b, n, h + 32 * p);
		}
	}
	free(order);
	free(syms);
	free(sorted);
	if (nerr) {
		free(img);
		goto fail;
	}

	/* write: a fresh file (a running binary must not be overwritten in place) */
	{
		char tmp[4096];
		snprintf(tmp, sizeof tmp, "%s.vld-tmp.%d", out, (int)getpid());
		int fd = openat(dirfd, tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0777);
		if (fd < 0) {
			error("cannot create %s: %s", tmp, strerror(errno));
			free(img);
			goto fail;
		}
		size_t done = 0;
		while (done < filesize) {
			ssize_t w = write(fd, img + done, filesize - done);
			if (w <= 0) {
				error("cannot write %s: %s", tmp, strerror(errno));
				break;
			}
			done += (size_t)w;
		}
		close(fd);
		free(img);
		if (done != filesize || renameat(dirfd, tmp, dirfd, out)) {
			if (done == filesize)
				error("cannot rename to %s: %s", out, strerror(errno));
			unlinkat(dirfd, tmp, 0);
			goto fail;
		}
	}
	free(inputs);
	reset();
	return 0;
fail:
	free(inputs);
	reset();
	return 1;
}
