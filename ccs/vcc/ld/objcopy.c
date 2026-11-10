/*
 * vobjcopy / vnm: the objcopy and nm uses of the arm64 kernel build
 * (PLAN-fas2 step 2.8), producing the same bytes as llvm-objcopy/llvm-nm 23.
 *
 * objcopy, as used by the build:
 *   --remove-section=.modinfo -w --strip-unneeded-symbol='__mod_device_table__*'
 *   -O binary -R .note -R .note.gnu.build-id -R .comment -S      (Image)
 *   -O binary -j .modinfo --dump-section .modinfo=FILE ... /dev/null
 *   -S                                                           (vdso.so)
 *   --prefix-symbols=__pi_ --remove-section=.note.gnu.property
 *     [--prefix-alloc-sections=.init]                            (pi/X.pi.o)
 *
 * The ELF writer follows llvm-objcopy: segments keep their bytes and move
 * only as a whole (layoutSegments), sections inside a segment keep their
 * place in it, the rest are packed after the segments in original offset
 * order, non-alloc string tables are rebuilt with StringTableBuilder's
 * suffix-sorted, tail-merged layout, and removed sections inside segments
 * are zeroed.
 *
 * nm: BSD output, -p (no sort), -n (numeric), -S (sizes), default sort by
 * (name, size, address); symbols that llvm-nm treats as format specific
 * (null, FILE, SECTION, $d/$x mapping symbols) are not printed.
 */
#include "ld.h"
#include "../lib/hash.h"
#include "ldelf.h"

#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define NONE 0xffffffffu

static FILE *oerr;
static int oerrors;
static const char *prog = "vobjcopy";

static void oerror(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void oerror(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(oerr, "%s: error: ", prog);
	vfprintf(oerr, fmt, ap);
	fputc('\n', oerr);
	va_end(ap);
	oerrors++;
}

static void *xcalloc(size_t n, size_t s)
{
	void *p = calloc(n ? n : 1, s ? s : 1);
	if (!p) {
		fprintf(stderr, "%s: out of memory\n", prog);
		abort();
	}
	return p;
}

static uint64_t alignto(uint64_t v, uint64_t a) { return a <= 1 ? v : (v + a - 1) / a * a; }
static uint64_t alignskew(uint64_t v, uint64_t a, uint64_t skew)
{
	if (a <= 1)
		return v;
	skew %= a;
	return (v + a - 1 - skew) / a * a + skew;
}

/* ---------------- input ---------------- */

typedef struct {
	const uint8_t *buf;
	size_t size;
	const Ehdr *eh;
	const Shdr *sh;
	uint32_t nsh;
	const Phdr *ph;
	uint32_t nph;
	const char *shstr;
} Elf;

/* a table of n entries of sz bytes at off, inside the file and 8-aligned */
static bool in_file(const Elf *e, uint64_t off, uint64_t n, uint64_t sz)
{
	return off % 8 == 0 && off <= e->size && n <= (e->size - off) / sz;
}

/* everything elf_open's users index, checked once (as vlink's obj_valid) */
static bool elf_valid(Elf *e)
{
	const Ehdr *h = e->eh;

	if ((h->e_shnum && h->e_shentsize != sizeof(Shdr)) || (h->e_phnum && h->e_phentsize != sizeof(Phdr)) ||
	    !in_file(e, h->e_phoff, h->e_phnum, sizeof(Phdr)) || !in_file(e, h->e_shoff, h->e_shnum, sizeof(Shdr)))
		return false;
	e->sh = (const Shdr *)(e->buf + h->e_shoff);
	e->nsh = h->e_shnum;
	e->ph = (const Phdr *)(e->buf + h->e_phoff);
	e->nph = h->e_phnum;
	e->shstr = "";
	for (uint32_t k = 0; k < e->nph; k++)
		if (e->ph[k].p_offset > e->size || e->ph[k].p_filesz > e->size - e->ph[k].p_offset)
			return false;
	if (!e->nsh)
		return h->e_shoff == 0 || h->e_shstrndx == 0;
	if (h->e_shstrndx >= e->nsh)
		return false;
	for (uint32_t k = 1; k < e->nsh; k++) {
		const Shdr *s = &e->sh[k];

		if (s->sh_type != SHT_NOBITS && (s->sh_offset > e->size || s->sh_size > e->size - s->sh_offset))
			return false;
		if (s->sh_addralign & (s->sh_addralign - 1))
			return false;
	}
	const Shdr *ss = &e->sh[h->e_shstrndx];

	if (ss->sh_type != SHT_STRTAB || !ss->sh_size || e->buf[ss->sh_offset + ss->sh_size - 1])
		return false;
	e->shstr = (const char *)e->buf + ss->sh_offset;
	for (uint32_t k = 0; k < e->nsh; k++) {
		const Shdr *s = &e->sh[k];

		if (s->sh_name >= ss->sh_size)
			return false;
		if (s->sh_type == SHT_SYMTAB || s->sh_type == SHT_DYNSYM) {
			if (s->sh_offset % 8 || s->sh_size % sizeof(Sym) || s->sh_link >= e->nsh)
				return false;
			const Shdr *st = &e->sh[s->sh_link];

			if (st->sh_type != SHT_STRTAB || !st->sh_size || e->buf[st->sh_offset + st->sh_size - 1])
				return false;
			const Sym *y = (const Sym *)(e->buf + s->sh_offset);

			for (uint64_t j = 0; j < s->sh_size / sizeof(Sym); j++)
				if (y[j].st_name >= st->sh_size ||
				    (y[j].st_shndx && y[j].st_shndx < 0xff00 && y[j].st_shndx >= e->nsh))
					return false;
		}
		if (s->sh_type == SHT_RELA &&
		    (s->sh_offset % 8 || s->sh_size % sizeof(Rela) || s->sh_link >= e->nsh || s->sh_info >= e->nsh))
			return false;
	}
	return true;
}

static int elf_open(Elf *e, int dirfd, const char *path)
{
	memset(e, 0, sizeof *e);
	int fd = openat(dirfd, path, O_RDONLY);
	if (fd < 0) {
		oerror("'%s': %s", path, strerror(errno));
		return -1;
	}
	struct stat st;
	fstat(fd, &st);
	e->size = st.st_size;
	e->buf = e->size ? mmap(NULL, e->size, PROT_READ, MAP_PRIVATE, fd, 0) : NULL;
	close(fd);
	if (e->buf == MAP_FAILED || e->size < sizeof(Ehdr) || memcmp(e->buf, "\177ELF\2\1", 6)) {
		oerror("'%s': not an ELF64 little-endian file", path);
		e->buf = NULL;
		return -1;
	}
	e->eh = (const Ehdr *)e->buf;
	if (!elf_valid(e)) {
		oerror("'%s': invalid ELF file", path);
		munmap((void *)e->buf, e->size);
		e->buf = NULL;
		return -1;
	}
	return 0;
}
static void elf_close(Elf *e)
{
	if (e->buf)
		munmap((void *)e->buf, e->size);
}

/* ---------------- StringTableBuilder (ELF, optimized) ---------------- */

typedef struct { const char *s; uint32_t len, h; uint64_t off; } StrEnt;
typedef struct {
	StrEnt *v;
	uint32_t n, cap;
	uint32_t *tab;			/* dedup hash: index+1 */
	uint32_t tmask;
	uint64_t size;
} StrTab;

static void st_grow(StrTab *t)
{
	uint32_t nm = t->tab ? (t->tmask + 1) * 2 : 1024;
	uint32_t *nt = xcalloc(nm, 4);
	for (uint32_t i = 0; i < t->n; i++) {
		uint32_t j = t->v[i].h & (nm - 1);
		while (nt[j])
			j = (j + 1) & (nm - 1);
		nt[j] = i + 1;
	}
	free(t->tab);
	t->tab = nt;
	t->tmask = nm - 1;
}
static void st_reserve(StrTab *t, uint32_t n)
{
	uint32_t m = 1024;
	while (m < n * 2 + 2)
		m *= 2;
	if (t->tab && t->tmask + 1 >= m)
		return;
	t->v = realloc(t->v, (t->cap = n > t->cap ? n : t->cap) * sizeof(StrEnt));
	t->tmask = m / 2 - 1;	/* st_grow doubles */
	uint32_t *old = t->tab;
	t->tab = xcalloc(1, 4);
	st_grow(t);
	free(old);
}
static uint32_t st_add(StrTab *t, const char *s)
{
	uint32_t len = strlen(s), h = vhash_strtab(s, len);
	if (!t->tab || (t->n + 1) * 2 > t->tmask)
		st_grow(t);
	uint32_t j = h & t->tmask;
	while (t->tab[j]) {
		StrEnt *e = &t->v[t->tab[j] - 1];
		if (e->h == h && e->len == len && !memcmp(e->s, s, len))
			return t->tab[j] - 1;
		j = (j + 1) & t->tmask;
	}
	if (t->n == t->cap) {
		t->cap = t->cap ? t->cap * 2 : 256;
		t->v = realloc(t->v, t->cap * sizeof(StrEnt));
	}
	t->v[t->n] = (StrEnt){.s = s, .len = len, .h = h};
	t->tab[j] = ++t->n;
	return t->n - 1;
}
static uint64_t st_find(const StrTab *t, const char *s)
{
	uint32_t len = strlen(s), h = vhash_strtab(s, len);
	if (!len)
		return 0;
	uint32_t j = h & t->tmask;
	while (t->tab[j]) {
		const StrEnt *e = &t->v[t->tab[j] - 1];
		if (e->h == h && e->len == len && !memcmp(e->s, s, len))
			return e->off;
		j = (j + 1) & t->tmask;
	}
	return 0;
}
/* multikeySort order: by characters from the end, larger first, a string
   before its own suffixes; a total order on distinct strings.  That is
   descending lexicographic order of the reversed strings, so each entry
   carries its reversed name (one shared buffer) and comparisons are
   memcmp, with the first 8 bytes as an integer key. */
typedef struct { uint64_t key; const char *r; uint32_t len; StrEnt *e; } SortEnt;
static int st_cmp(const void *a, const void *b)
{
	const SortEnt *p = a, *q = b;
	if (p->key != q->key)
		return p->key < q->key ? 1 : -1;
	uint32_t n = p->len < q->len ? p->len : q->len;
	if (n > 8) {
		int c = memcmp(p->r + 8, q->r + 8, n - 8);
		if (c)
			return -c;
	}
	return (p->len < q->len) - (p->len > q->len);
}
static void st_finalize(StrTab *t)
{
	SortEnt *p = xcalloc(t->n, sizeof *p);
	size_t tot = 0;
	for (uint32_t i = 0; i < t->n; i++)
		tot += t->v[i].len;
	char *rev = xcalloc(tot + 8, 1);
	size_t ro = 0;
	for (uint32_t i = 0; i < t->n; i++) {
		StrEnt *e = &t->v[i];
		char *r = rev + ro;
		for (uint32_t j = 0; j < e->len; j++)
			r[j] = e->s[e->len - 1 - j];
		uint64_t k = 0;
		for (uint32_t j = 0; j < 8; j++)
			k = k << 8 | (j < e->len ? (uint8_t)r[j] : 0);
		p[i] = (SortEnt){k, r, e->len, e};
		ro += e->len;
	}
	/* LSD radix sort on the key, descending; ties (same last 8 bytes)
	   are then sorted by the full comparison */
	SortEnt *tmp = xcalloc(t->n, sizeof *tmp);
	for (int sh = 0; sh < 64; sh += 8) {
		uint32_t cnt[257] = {0};
		for (uint32_t i = 0; i < t->n; i++)
			cnt[255 - (p[i].key >> sh & 255) + 1]++;
		bool one = false;
		for (int i = 1; i < 257; i++)
			one |= cnt[i] == t->n;
		if (one)
			continue;
		for (int i = 0; i < 256; i++)
			cnt[i + 1] += cnt[i];
		for (uint32_t i = 0; i < t->n; i++)
			tmp[cnt[255 - (p[i].key >> sh & 255)]++] = p[i];
		SortEnt *x = p;
		p = tmp;
		tmp = x;
	}
	free(tmp);
	for (uint32_t i = 0, j; i < t->n; i = j) {
		for (j = i + 1; j < t->n && p[j].key == p[i].key; j++)
			;
		if (j - i > 1 && p[i].len >= 8)
			qsort(p + i, j - i, sizeof *p, st_cmp);
	}
	t->size = 1;
	const StrEnt *prev = NULL;
	for (uint32_t i = 0; i < t->n; i++) {
		StrEnt *e = p[i].e;
		if (prev && prev->len >= e->len && !memcmp(prev->s + prev->len - e->len, e->s, e->len)) {
			e->off = t->size - e->len - 1;
			continue;
		}
		e->off = t->size;
		t->size += e->len + 1;
		prev = e;
	}
	free(p);
	free(rev);
}
static void st_write(const StrTab *t, uint8_t *dst)
{
	memset(dst, 0, t->size);
	for (uint32_t i = 0; i < t->n; i++)
		memcpy(dst + t->v[i].off, t->v[i].s, t->v[i].len);
}
static void st_free(StrTab *t)
{
	free(t->v);
	free(t->tab);
}

/* ---------------- objcopy model ---------------- */

enum { SK_RAW, SK_SYMTAB, SK_STRTAB, SK_RELA };

typedef struct {
	const char *name;
	char *newname;
	uint32_t type, link, info;
	uint64_t flags, addr, origoff, size, align, entsize;
	const uint8_t *data;
	int parent;			/* segment or -1 */
	bool removed;
	uint8_t kind;
	uint32_t idx;			/* new index */
	uint64_t off;			/* new offset */
	uint8_t *gen;			/* generated contents */
} OSec;

typedef struct {
	Phdr p;
	uint64_t origoff, off;
	uint64_t filesz, vaddr, align;
	uint32_t index;
	int parent;
} OSeg;

typedef struct {
	const char *name;
	char *newname;
	uint8_t info, other;
	uint16_t shndx;			/* original */
	uint64_t value, size;
	bool removed;
	uint32_t idx, sidx;		/* new index, string table entry */
} OSym;

typedef struct {
	const char *pat;
} Pattern;

static struct {
	const char *in, *out;
	bool binary, strip_all, strip_debug, wildcard;
	const char *prefix_syms, *prefix_alloc;
	const char **remove;
	uint32_t nremove;
	const char **redef;	/* "old=new" */
	uint32_t nredef;
	const char **add;	/* --add-section name=file */
	uint32_t nadd;
	const char *debuglink;
	const char **only;
	uint32_t nonly;
	const char **unneeded;
	uint32_t nunneeded;
	const char **dump;
	uint32_t ndump;
} oc;

static bool match_list(const char **v, uint32_t n, const char *name)
{
	for (uint32_t i = 0; i < n; i++) {
		const char *p = v[i];
		bool neg = false;
		if (oc.wildcard && *p == '!') {
			neg = true;
			p++;
		}
		bool m;
		size_t meta = 0;
		while (p[meta] && !(oc.wildcard && (p[meta] == '*' || p[meta] == '?' || p[meta] == '[' || p[meta] == '\\')))
			meta++;
		if (!p[meta])
			m = !strcmp(p, name);
		else if (p[meta] == '*' && !p[meta + 1])	/* prefix* */
			m = !strncmp(p, name, meta);
		else
			m = fnmatch(p, name, 0) == 0;
		if (m)
			return !neg;
	}
	return false;
}

static const char *sname(OSec *s) { return s->newname ? s->newname : s->name; }
static const char *yname(OSym *y) { return y->newname ? y->newname : y->name; }

static bool sec_in_seg(const OSec *s, const OSeg *g)
{
	uint64_t sz = s->size ? s->size : 1;
	if (s->type == SHT_NOBITS) {
		if (!(s->flags & SHF_ALLOC))
			return false;
		if (((s->flags & SHF_TLS) != 0) != (g->p.p_type == 7))
			return false;
		return g->p.p_vaddr <= s->addr && g->p.p_vaddr + g->p.p_memsz >= s->addr + sz;
	}
	return g->origoff <= s->origoff && g->origoff + g->filesz >= s->origoff + sz;
}
static bool seg_before(const OSeg *a, const OSeg *b)
{
	if (a->origoff != b->origoff)
		return a->origoff < b->origoff;
	return a->index < b->index;
}

static int write_file(int dirfd, const char *path, const uint8_t *b, uint64_t n, int mode)
{
	if (!strcmp(path, "/dev/null"))
		return 0;
	char tmp[4096];
	snprintf(tmp, sizeof tmp, "%s.vobjcopy-tmp", path);
	int fd = openat(dirfd, tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0) {
		oerror("'%s': %s", path, strerror(errno));
		return 1;
	}
	uint64_t off = 0;
	while (off < n) {
		ssize_t w = write(fd, b + off, n - off > (1u << 30) ? (1u << 30) : n - off);
		if (w <= 0) {
			oerror("write '%s': %s", path, strerror(errno));
			close(fd);
			return 1;
		}
		off += w;
	}
	close(fd);
	/* rename over the target: the input may be the output */
	if (renameat(dirfd, tmp, dirfd, path)) {
		oerror("rename '%s': %s", path, strerror(errno));
		return 1;
	}
	return 0;
}

static int objcopy_run(int dirfd)
{
	Elf e;
	if (elf_open(&e, dirfd, oc.in))
		return 1;
	const Ehdr *eh = e.eh;
	bool rel = eh->e_type == 1;
	uint32_t ns = e.nsh;
	OSec *sec = xcalloc(ns + oc.nadd + 1, sizeof *sec);
	for (uint32_t i = 0; i < ns; i++) {
		const Shdr *h = &e.sh[i];
		OSec *s = &sec[i];
		s->name = e.shstr + h->sh_name;
		s->type = h->sh_type;
		s->flags = h->sh_flags;
		s->addr = h->sh_addr;
		s->origoff = h->sh_offset;
		s->size = h->sh_size;
		s->link = h->sh_link;
		s->info = h->sh_info;
		s->align = h->sh_addralign;
		s->entsize = h->sh_entsize;
		s->data = h->sh_type == SHT_NOBITS ? NULL : e.buf + h->sh_offset;
		s->parent = -1;
		if (h->sh_type == SHT_SYMTAB)
			s->kind = SK_SYMTAB;
		else if (h->sh_type == SHT_STRTAB && !(h->sh_flags & SHF_ALLOC))
			s->kind = SK_STRTAB;
		else if (h->sh_type == SHT_RELA && h->sh_link && e.sh[h->sh_link].sh_type == SHT_SYMTAB)
			s->kind = SK_RELA;
	}
	if (!rel && e.sh[0].sh_size)
		oerror("extended section numbering not supported");
	/* --add-section: at the end, after everything else (llvm-objcopy: PROGBITS, or NOTE for .note*) */
	for (uint32_t a = 0; a < oc.nadd; a++) {
		const char *eq = strchr(oc.add[a], '=');
		OSec *s = &sec[ns];
		struct stat st;
		int fd = openat(dirfd, eq + 1, O_RDONLY);

		if (fd < 0 || fstat(fd, &st) < 0) {
			oerror("'%s': %s", eq + 1, strerror(errno));
			if (fd >= 0)
				close(fd);
			continue;
		}
		s->newname = strndup(oc.add[a], (size_t)(eq - oc.add[a]));
		s->name = s->newname;
		s->type = !strncmp(s->name, ".note", 5) && strcmp(s->name, ".note.GNU-stack") ? SHT_NOTE : SHT_PROGBITS;
		s->align = 1;
		s->size = (uint64_t)st.st_size;
		s->origoff = UINT64_MAX;
		s->parent = -1;
		s->gen = xcalloc(s->size + 1, 1);
		if (s->size && read(fd, s->gen, s->size) != (ssize_t)s->size)
			oerror("'%s': short read", eq + 1);
		if (s->type == SHT_NOTE && s->size && s->size < 12)
			oerror("'%s': %s data must be either empty or at least 12 bytes long", oc.in, s->name);
		close(fd);
		ns++;
	}
	/* --add-gnu-debuglink (after --add-section): .gnu_debuglink = basename, NUL, padding to 4, CRC-32 of the file */
	if (oc.debuglink) {
		OSec *s = &sec[ns];
		int fd = openat(dirfd, oc.debuglink, O_RDONLY);
		struct stat st;
		const char *base = strrchr(oc.debuglink, '/') ? strrchr(oc.debuglink, '/') + 1 : oc.debuglink;
		size_t bl = strlen(base), sz = (bl + 4) / 4 * 4 + 4;
		uint32_t crc = 0xffffffff;

		if (fd < 0 || fstat(fd, &st) < 0) {
			oerror("'%s': %s", oc.debuglink, strerror(errno));
		} else {
			uint8_t buf[65536];
			ssize_t r;

			while ((r = read(fd, buf, sizeof buf)) > 0)
				for (ssize_t i = 0; i < r; i++) {
					crc ^= buf[i];
					for (int b = 0; b < 8; b++)
						crc = crc >> 1 ^ (0xedb88320 & -(crc & 1));
				}
			crc = ~crc;
			s->name = ".gnu_debuglink";
			s->type = SHT_PROGBITS;
			s->align = 4;
			s->size = sz;
			s->origoff = UINT64_MAX;
			s->parent = -1;
			s->gen = xcalloc(sz, 1);
			memcpy(s->gen, base, bl);
			memcpy(s->gen + sz - 4, &crc, 4);
			ns++;
		}
		if (fd >= 0)
			close(fd);
	}

	/* segments: real ones, then the ELF header and program header "segments" */
	uint32_t ng = e.nph + 2;
	OSeg *seg = xcalloc(ng, sizeof *seg);
	for (uint32_t i = 0; i < e.nph; i++) {
		seg[i].p = e.ph[i];
		seg[i].origoff = seg[i].off = e.ph[i].p_offset;
		seg[i].filesz = e.ph[i].p_filesz;
		seg[i].vaddr = e.ph[i].p_vaddr;
		seg[i].align = e.ph[i].p_align;
		seg[i].index = i;
		seg[i].parent = -1;
		for (uint32_t k = 1; k < ns; k++)
			if (sec_in_seg(&sec[k], &seg[i]))
				if (sec[k].parent < 0 || seg[sec[k].parent].origoff > seg[i].origoff)
					sec[k].parent = i;
	}
	uint32_t ehs = e.nph, phs = e.nph + 1;
	seg[ehs] = (OSeg){.origoff = 0, .filesz = sizeof(Ehdr), .index = ehs, .parent = -1};
	seg[phs] = (OSeg){.origoff = eh->e_phoff, .vaddr = eh->e_phoff, .filesz = (uint64_t)eh->e_phentsize * eh->e_phnum,
			  .align = 8, .index = phs, .parent = -1};
	for (uint32_t c = 0; c < ng; c++) {
		OSeg *ch = &seg[c];
		for (uint32_t p = 0; p < e.nph; p++) {
			OSeg *pa = &seg[p];
			if (p == c || !(pa->origoff <= ch->origoff && pa->origoff + pa->filesz > ch->origoff))
				continue;
			if (seg_before(pa, ch) && (ch->parent < 0 || seg_before(pa, &seg[ch->parent])))
				ch->parent = p;
		}
	}

	/* --dump-section */
	for (uint32_t i = 0; i < oc.ndump; i++) {
		char *eq = strchr(oc.dump[i], '=');
		if (!eq) {
			oerror("bad --dump-section %s", oc.dump[i]);
			continue;
		}
		size_t nl = eq - oc.dump[i];
		uint32_t k;
		for (k = 1; k < ns; k++)
			if (strlen(sec[k].name) == nl && !memcmp(sec[k].name, oc.dump[i], nl))
				break;
		if (k == ns) {
			oerror("section '%.*s' not found", (int)nl, oc.dump[i]);
			continue;
		}
		if (sec[k].type == SHT_NOBITS) {
			oerror("cannot dump section '%s': it has no contents", sec[k].name);
			continue;
		}
		write_file(dirfd, eq + 1, sec[k].data, sec[k].size, 0666);
	}

	/* section removal (replaceAndRemoveSections) */
	uint32_t shstr_i = eh->e_shstrndx;
	uint32_t symtab_i = NONE;
	for (uint32_t k = 1; k < ns; k++)
		if (sec[k].kind == SK_SYMTAB)
			symtab_i = k;
	uint32_t symstr_i = symtab_i != NONE ? sec[symtab_i].link : NONE;
	for (uint32_t k = 1; k < ns; k++) {
		OSec *s = &sec[k];
		bool rm = oc.nremove && match_list(oc.remove, oc.nremove, s->name);
		if (oc.strip_all && !rm && k != shstr_i && strncmp(s->name, ".gnu.warning", 12) &&
		    strncmp(s->name, ".gnu_debuglink", 14) && s->type != 0x70000003 && s->parent < 0)
			rm = !(s->flags & SHF_ALLOC);
		if (oc.strip_debug && !(s->flags & SHF_ALLOC) &&
		    (!strncmp(s->name, ".debug", 6) || !strncmp(s->name, ".zdebug", 7) || !strcmp(s->name, ".gdb_index")))
			rm = true;	/* llvm-objcopy's isDebugSection */
		if (oc.nonly) {
			if (match_list(oc.only, oc.nonly, s->name))
				rm = false;
			else if (!rm)
				rm = !(k == shstr_i || k == symtab_i || k == symstr_i);
		}
		s->removed = rm;
	}
	for (uint32_t k = 1; k < ns; k++) {
		OSec *s = &sec[k];
		if (!s->removed && (s->type == SHT_RELA || s->type == SHT_REL) && (s->flags & SHF_INFO_LINK || s->kind == SK_RELA) &&
		    s->info && s->info < ns && sec[s->info].removed)
			s->removed = true;
	}
	if (symtab_i != NONE && sec[symtab_i].removed)
		symtab_i = NONE;
	if (sec[shstr_i].removed) {
		oerror("cannot remove the section header string table");
		return 1;
	}

	/* symbols */
	OSym *sym = NULL;
	uint32_t nsym = 0;
	if (symtab_i != NONE) {
		OSec *st = &sec[symtab_i];
		const Sym *ys = (const Sym *)st->data;
		const char *str = (const char *)e.buf + e.sh[st->link].sh_offset;
		nsym = st->size / sizeof(Sym);
		sym = xcalloc(nsym, sizeof *sym);
		for (uint32_t i = 0; i < nsym; i++) {
			sym[i].name = str + ys[i].st_name;
			sym[i].info = ys[i].st_info;
			sym[i].other = ys[i].st_other;
			sym[i].shndx = ys[i].st_shndx;
			sym[i].value = ys[i].st_value;
			sym[i].size = ys[i].st_size;
			if (i && sym[i].shndx && sym[i].shndx < 0xff00 && sym[i].shndx < ns && sec[sym[i].shndx].removed)
				sym[i].removed = true;
		}
		bool *referenced = NULL;
		if (oc.nunneeded && rel) {
			referenced = xcalloc(nsym, 1);
			for (uint32_t k = 1; k < ns; k++) {
				if (sec[k].removed || sec[k].kind != SK_RELA)
					continue;
				const Rela *r = (const Rela *)sec[k].data;
				for (uint64_t j = 0; j < sec[k].size / sizeof(Rela); j++)
					if ((r[j].r_info >> 32) < nsym)
						referenced[r[j].r_info >> 32] = true;
			}
		}
		for (uint32_t i = 1; i < nsym; i++) {
			OSym *y = &sym[i];
			uint8_t type = y->info & 15, bind = y->info >> 4;
			for (uint32_t r = 0; r < oc.nredef && type != STT_SECTION; r++) {
				const char *eq = strchr(oc.redef[r], '=');

				if ((size_t)(eq - oc.redef[r]) == strlen(y->name) && !strncmp(y->name, oc.redef[r], eq - oc.redef[r])) {
					free(y->newname);
					y->newname = strdup(eq + 1);
					break;
				}
			}
			if (oc.prefix_syms && type != STT_SECTION) {
				const char *nm = y->newname ? y->newname : y->name;
				size_t a = strlen(oc.prefix_syms), b = strlen(nm);
				char *old = y->newname;
				y->newname = xcalloc(a + b + 1, 1);
				memcpy(y->newname, oc.prefix_syms, a);
				memcpy(y->newname + a, nm, b + 1);
				free(old);
			}
			if (oc.strip_all || (oc.strip_debug && type == STT_FILE))
				y->removed = true;
			else if (oc.nunneeded && match_list(oc.unneeded, oc.nunneeded, yname(y))) {
				bool unneeded = !referenced || (!referenced[i] && (bind == STB_LOCAL || y->shndx == SHN_UNDEF) &&
								type != STT_FILE && type != STT_SECTION);
				if (!rel || unneeded)
					y->removed = true;
			}
		}
		free(referenced);
		uint32_t idx = 0;
		for (uint32_t i = 0; i < nsym; i++)
			if (!sym[i].removed)
				sym[i].idx = idx++;
		/* a removed symbol must not be referenced by a kept relocation */
		for (uint32_t k = 1; k < ns; k++) {
			if (sec[k].removed || sec[k].kind != SK_RELA)
				continue;
			const Rela *r = (const Rela *)sec[k].data;
			for (uint64_t j = 0; j < sec[k].size / sizeof(Rela); j++) {
				uint32_t si = r[j].r_info >> 32;
				if (si && (si >= nsym || sym[si].removed)) {
					oerror("not stripping symbol '%s' because it is named in a relocation",
					       si < nsym ? sym[si].name : "?");
					return 1;
				}
			}
		}
	}

	/* --prefix-alloc-sections */
	if (oc.prefix_alloc) {
		size_t pl = strlen(oc.prefix_alloc);
		for (uint32_t k = 1; k < ns; k++) {
			OSec *s = &sec[k];
			if (s->removed)
				continue;
			const char *base = NULL, *rp = NULL;
			if (s->flags & SHF_ALLOC) {
				base = s->name;
				rp = "";
			} else if (s->kind == SK_RELA && s->info && (sec[s->info].flags & SHF_ALLOC)) {
				base = sec[s->info].name;
				rp = ".rela";
			}
			if (!base)
				continue;
			size_t n = strlen(rp) + pl + strlen(base) + 1;
			s->newname = xcalloc(n, 1);
			snprintf(s->newname, n, "%s%s%s", rp, oc.prefix_alloc, base);
		}
	}

	if (oc.binary) {
		/* BinaryWriter: alloc sections with contents at LMA - min LMA */
		uint64_t min = UINT64_MAX, total = 0;
		uint64_t *lma = xcalloc(ns, 8);
		for (uint32_t k = 1; k < ns; k++) {
			OSec *s = &sec[k];
			if (s->removed || !(s->flags & SHF_ALLOC))
				continue;
			lma[k] = s->addr;
			if (s->parent >= 0)
				lma[k] = s->origoff - seg[s->parent].origoff + seg[s->parent].p.p_paddr;
			if (s->type != SHT_NOBITS && s->size && lma[k] < min)
				min = lma[k];
		}
		for (uint32_t k = 1; k < ns; k++) {
			OSec *s = &sec[k];
			if (s->removed || !(s->flags & SHF_ALLOC) || s->type == SHT_NOBITS || !s->size)
				continue;
			if (lma[k] - min + s->size > total)
				total = lma[k] - min + s->size;
		}
		uint8_t *b = total ? mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0) : NULL;
		for (uint32_t k = 1; k < ns; k++) {
			OSec *s = &sec[k];
			if (s->removed || !(s->flags & SHF_ALLOC) || s->type == SHT_NOBITS || !s->size)
				continue;
			memcpy(b + lma[k] - min, s->data, s->size);
		}
		int rc = oerrors ? 1 : write_file(dirfd, oc.out, b, total, 0777);
		if (b)
			munmap(b, total);
		free(lma);
		free(sec);
		free(seg);
		free(sym);
		elf_close(&e);
		return rc;
	}

	/* indices of kept sections */
	uint32_t nkept = 0;
	for (uint32_t k = 1; k < ns; k++)
		if (!sec[k].removed)
			sec[k].idx = ++nkept;
	/* string tables */
	StrTab names = {0}, symnames = {0};
	bool shared_str = symtab_i != NONE && sec[symtab_i].link == shstr_i;
	for (uint32_t k = 1; k < ns; k++)
		if (!sec[k].removed)
			st_add(&names, sname(&sec[k]));
	StrTab *sn = shared_str ? &names : &symnames;
	st_reserve(sn, nsym + (shared_str ? ns : 0));
	if (symtab_i != NONE)
		for (uint32_t i = 0; i < nsym; i++)
			if (!sym[i].removed)
				sym[i].sidx = st_add(sn, yname(&sym[i]));
	st_finalize(&names);
	if (!shared_str && symtab_i != NONE)
		st_finalize(&symnames);
	/* generated sections */
	uint32_t nsymout = 0, maxlocal = 0;
	for (uint32_t i = 0; i < nsym; i++)
		if (!sym[i].removed) {
			nsymout++;
			if ((sym[i].info >> 4) == STB_LOCAL && sym[i].idx > maxlocal)
				maxlocal = sym[i].idx;
		}
	for (uint32_t k = 1; k < ns; k++) {
		OSec *s = &sec[k];
		if (s->removed)
			continue;
		if (k == symtab_i) {
			s->size = (uint64_t)nsymout * sizeof(Sym);
			s->gen = xcalloc(s->size, 1);
			Sym *d = (Sym *)s->gen;
			for (uint32_t i = 0; i < nsym; i++) {
				OSym *y = &sym[i];
				if (y->removed)
					continue;
				uint16_t shndx = y->shndx;
				if (shndx && shndx < 0xff00)
					shndx = shndx < ns ? sec[shndx].idx : 0;
				d[y->idx] = (Sym){.st_name = sn->v[y->sidx].len ? (uint32_t)sn->v[y->sidx].off : 0, .st_info = y->info,
						  .st_other = y->other, .st_shndx = shndx, .st_value = y->value,
						  .st_size = y->size};
			}
			s->link = sec[s->link].idx;
			s->info = maxlocal + 1;
		} else if (s->kind == SK_STRTAB) {
			StrTab *t = k == shstr_i ? &names : (symtab_i != NONE && k == symstr_i) ? sn : NULL;
			if (!t) {
				/* an unreferenced string table: llvm-objcopy empties it */
				s->size = 1;
				s->gen = xcalloc(1, 1);
				continue;
			}
			s->size = t->size;
			s->gen = xcalloc(t->size, 1);
			st_write(t, s->gen);
		} else if (s->kind == SK_RELA) {
			s->gen = xcalloc(s->size, 1);
			const Rela *r = (const Rela *)s->data;
			Rela *d = (Rela *)s->gen;
			for (uint64_t j = 0; j < s->size / sizeof(Rela); j++) {
				uint32_t si = r[j].r_info >> 32;
				d[j] = r[j];
				d[j].r_info = (uint64_t)(si ? sym[si].idx : 0) << 32 | (uint32_t)r[j].r_info;
			}
			s->link = symtab_i != NONE ? sec[symtab_i].idx : 0;
			s->info = s->info ? sec[s->info].idx : 0;
		} else if (s->link) {
			s->link = s->link < ns && !sec[s->link].removed ? sec[s->link].idx : 0;
		}
	}

	/* layout: segments, then sections outside segments by original offset */
	uint32_t *order = xcalloc(ng, 4);
	for (uint32_t i = 0; i < ng; i++)
		order[i] = i;
	for (uint32_t i = 1; i < ng; i++) {
		uint32_t x = order[i], j = i;
		while (j > 0 && seg_before(&seg[x], &seg[order[j - 1]])) {
			order[j] = order[j - 1];
			j--;
		}
		order[j] = x;
	}
	uint64_t off = 0;
	for (uint32_t i = 0; i < ng; i++) {
		OSeg *g = &seg[order[i]];
		if (g->parent >= 0)
			g->off = seg[g->parent].off + g->origoff - seg[g->parent].origoff;
		else
			g->off = alignskew(off, g->align ? g->align : 1, g->vaddr);
		if (g->off + g->filesz > off)
			off = g->off + g->filesz;
	}
	uint32_t *outside = xcalloc(ns, 4), nout = 0;
	for (uint32_t k = 1; k < ns; k++) {
		OSec *s = &sec[k];
		if (s->removed)
			continue;
		if (s->parent >= 0)
			s->off = seg[s->parent].off + s->origoff - seg[s->parent].origoff;
		else
			outside[nout++] = k;
	}
	for (uint32_t i = 1; i < nout; i++) {
		uint32_t x = outside[i], j = i;
		while (j > 0 && sec[outside[j - 1]].origoff > sec[x].origoff) {
			outside[j] = outside[j - 1];
			j--;
		}
		outside[j] = x;
	}
	for (uint32_t i = 0; i < nout; i++) {
		OSec *s = &sec[outside[i]];
		off = alignto(off, s->align ? s->align : 1);
		s->off = off;
		if (s->type != SHT_NOBITS)
			off += s->size;
	}
	uint64_t shoff = alignto(off, 8);
	uint64_t total = shoff + (uint64_t)(nkept + 1) * sizeof(Shdr);
	uint8_t *b = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

	/* segment bytes, then zero removed sections inside segments */
	for (uint32_t i = 0; i < e.nph; i++)
		memcpy(b + seg[i].off, e.buf + seg[i].origoff, seg[i].filesz);
	for (uint32_t k = 1; k < ns; k++) {
		OSec *s = &sec[k];
		if (!s->removed || s->parent < 0 || s->type == SHT_NOBITS || !s->size)
			continue;
		memset(b + s->origoff - seg[s->parent].origoff + seg[s->parent].off, 0, s->size);
	}
	Ehdr *oh = (Ehdr *)b;
	memset(oh->e_ident, 0, 16);
	memcpy(oh->e_ident, "\177ELF\2\1\1", 7);
	oh->e_ident[7] = eh->e_ident[7];
	oh->e_ident[8] = eh->e_ident[8];
	oh->e_type = eh->e_type;
	oh->e_machine = eh->e_machine;
	oh->e_version = eh->e_version;
	oh->e_entry = eh->e_entry;
	oh->e_phnum = e.nph;
	oh->e_phoff = e.nph ? seg[phs].off : 0;
	oh->e_phentsize = e.nph ? sizeof(Phdr) : 0;
	oh->e_flags = eh->e_flags;
	oh->e_ehsize = sizeof(Ehdr);
	oh->e_shentsize = sizeof(Shdr);
	oh->e_shoff = shoff;
	oh->e_shnum = nkept + 1;
	oh->e_shstrndx = sec[shstr_i].idx;
	for (uint32_t i = 0; i < e.nph; i++) {
		Phdr *p = (Phdr *)(b + seg[phs].off) + i;
		*p = seg[i].p;
		p->p_offset = seg[i].off;
	}
	Shdr *sh = (Shdr *)(b + shoff);
	/* llvm-objcopy: .llvm_addrsig's sh_link is dropped once sections or symbols may be removed */
	bool changed = oc.strip_all || oc.strip_debug || oc.nremove || oc.nonly || oc.nunneeded;
	for (uint32_t k = 1; k < ns; k++) {
		OSec *s = &sec[k];
		if (s->removed)
			continue;
		if (s->type != SHT_NOBITS && s->size)
			memcpy(b + s->off, s->gen ? s->gen : s->data, s->size);
		sh[s->idx] = (Shdr){.sh_name = (uint32_t)st_find(&names, sname(s)), .sh_type = s->type,
				    .sh_flags = s->flags, .sh_addr = s->addr, .sh_offset = s->off, .sh_size = s->size,
				    .sh_link = s->type == 0x6fff4c03 && changed ? 0 : s->link, .sh_info = s->info, .sh_addralign = s->align,
				    .sh_entsize = s->entsize};
	}
	int rc = oerrors ? 1 : write_file(dirfd, oc.out, b, total, rel ? 0666 : 0777);
	munmap(b, total);
	for (uint32_t k = 0; k < ns; k++) {
		free(sec[k].gen);
		free(sec[k].newname);
	}
	for (uint32_t i = 0; i < nsym; i++)
		free(sym[i].newname);
	st_free(&names);
	st_free(&symnames);
	free(order);
	free(outside);
	free(sec);
	free(seg);
	free(sym);
	elf_close(&e);
	return rc;
}

static void push(const char ***v, uint32_t *n, const char *s)
{
	*v = realloc(*v, (*n + 1) * sizeof(char *));
	(*v)[(*n)++] = s;
}

int objcopy_main(int argc, char **argv, int dirfd, FILE *err)
{
	oerr = err ? err : stderr;
	oerrors = 0;
	prog = "vobjcopy";
	memset(&oc, 0, sizeof oc);
	const char *files[2];
	int nf = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define OPT(s) (!strcmp(a, s))
#define VAL(s) (!strncmp(a, s "=", sizeof(s)) ? a + sizeof(s) : (OPT(s) && i + 1 < argc) ? argv[++i] : NULL)
		const char *v;
		if (OPT("-O") && i + 1 < argc) {
			oc.binary = !strcmp(argv[++i], "binary");
			if (!oc.binary)
				oerror("unsupported output format %s", argv[i]);
		} else if (!strncmp(a, "--output-target=", 16)) {
			oc.binary = !strcmp(a + 16, "binary");
		} else if (OPT("-S") || OPT("--strip-all")) {
			oc.strip_all = true;
		} else if (OPT("-g") || OPT("--strip-debug")) {
			oc.strip_debug = true;
		} else if (OPT("-w") || OPT("--wildcard")) {
			oc.wildcard = true;
		} else if ((v = VAL("--remove-section")) || (v = VAL("-R"))) {
			push(&oc.remove, &oc.nremove, v);
		} else if ((v = VAL("--only-section")) || (v = VAL("-j"))) {
			push(&oc.only, &oc.nonly, v);
		} else if ((v = VAL("--strip-unneeded-symbol"))) {
			push(&oc.unneeded, &oc.nunneeded, v);
		} else if ((v = VAL("--dump-section"))) {
			push(&oc.dump, &oc.ndump, v);
		} else if ((v = VAL("--add-gnu-debuglink"))) {
			oc.debuglink = v;
		} else if ((v = VAL("--add-section"))) {
			if (!strchr(v, '=') || v[0] == '=')
				oerror("bad format for --add-section: missing '='");
			push(&oc.add, &oc.nadd, v);
		} else if ((v = VAL("--redefine-sym"))) {
			if (!strchr(v, '='))
				oerror("bad format for --redefine-sym");
			push(&oc.redef, &oc.nredef, v);
		} else if ((v = VAL("--prefix-symbols"))) {
			oc.prefix_syms = v;
		} else if ((v = VAL("--prefix-alloc-sections"))) {
			oc.prefix_alloc = v;
		} else if (a[0] == '-' && a[1]) {
			oerror("unsupported option %s", a);
		} else if (nf < 2) {
			files[nf++] = a;
		} else {
			oerror("too many file arguments");
		}
#undef OPT
#undef VAL
	}
	if (nf < 1)
		oerror("no input file");
	int rc = 1;
	if (!oerrors) {
		oc.in = files[0];
		oc.out = nf > 1 ? files[1] : files[0];
		rc = objcopy_run(dirfd);
	}
	free(oc.remove);
	free(oc.only);
	free(oc.unneeded);
	free(oc.dump);
	free(oc.redef);
	free(oc.add);
	return rc || oerrors;
}

/* ================= nm ================= */

typedef struct {
	const char *name;
	uint64_t addr, size;
	char type;
	bool defined;
} NSym;

static int ncmp_name(const void *a, const void *b)
{
	const NSym *x = a, *y = b;
	int c = strcmp(x->name, y->name);
	if (c)
		return c;
	if (x->size != y->size)
		return x->size < y->size ? -1 : 1;
	return (x->addr > y->addr) - (x->addr < y->addr);
}
static int ncmp_num(const void *a, const void *b)
{
	const NSym *x = a, *y = b;
	if (x->defined != y->defined)
		return x->defined ? 1 : -1;
	if (x->addr != y->addr)
		return x->addr < y->addr ? -1 : 1;
	int c = strcmp(x->name, y->name);
	if (c)
		return c;
	return (x->size > y->size) - (x->size < y->size);
}

static char nm_type(const Elf *e, const Sym *y)
{
	uint8_t bind = y->st_info >> 4, type = y->st_info & 15;
	bool global = bind != STB_LOCAL;
	if (y->st_shndx == SHN_UNDEF) {
		if (bind != STB_WEAK)
			return 'U';
		return type == STT_OBJECT ? 'v' : 'w';
	}
	if (type == 10)
		return 'i';
	if (bind == STB_WEAK)
		return type == STT_OBJECT ? 'V' : 'W';
	if (y->st_shndx == SHN_COMMON)
		return 'C';
	char c = '?';
	if (y->st_shndx == SHN_ABS) {
		c = 'a';
	} else if (bind == 10) {
		return 'u';
	} else if (bind != STB_GLOBAL && bind != STB_LOCAL) {
		c = '?';
	} else if (y->st_shndx < e->nsh) {
		const Shdr *s = &e->sh[y->st_shndx];
		if (s->sh_flags & SHF_EXECINSTR)
			c = 't';
		else if (s->sh_type == SHT_NOBITS)
			c = 'b';
		else if (s->sh_flags & SHF_ALLOC)
			c = s->sh_flags & SHF_WRITE ? 'd' : 'r';
		else if (!strncmp(e->shstr + s->sh_name, ".debug", 6))
			c = 'N';
		else if (!(s->sh_flags & SHF_WRITE))
			c = 'n';
	}
	if (global && c >= 'a' && c <= 'z')
		c -= 32;
	return c;
}

int nm_main(int argc, char **argv, int dirfd, FILE *out, FILE *err)
{
	oerr = err ? err : stderr;
	oerrors = 0;
	prog = "vnm";
	bool nosort = false, numeric = false, size = false, undef = false, defd = false, ext = false;
	const char **files = xcalloc((size_t)argc + 1, sizeof *files);
	int nf = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] == '-' && a[1] && a[1] != '-') {
			for (const char *p = a + 1; *p; p++) {
				if (*p == 'p') nosort = true;
				else if (*p == 'n' || *p == 'v') numeric = true;
				else if (*p == 'S') size = true;
				else if (*p == 'u') undef = true;
				else if (*p == 'U') defd = true;
				else if (*p == 'g') ext = true;
				else oerror("unsupported option -%c", *p);
			}
		} else if (!strcmp(a, "--no-sort")) {
			nosort = true;
		} else if (!strcmp(a, "--numeric-sort")) {
			numeric = true;
		} else if (!strcmp(a, "--print-size")) {
			size = true;
		} else if (!strcmp(a, "--undefined-only")) {
			undef = true;
		} else if (!strcmp(a, "--defined-only")) {
			defd = true;
		} else if (!strcmp(a, "--extern-only")) {
			ext = true;
		} else if (a[0] == '-' && a[1]) {
			oerror("unknown argument '%s'", a);
		} else {
			files[nf++] = a;
		}
	}
	if (!nf)
		files[nf++] = "a.out";
	if (oerrors) {
		free(files);
		return 1;
	}
	for (int f = 0; f < nf; f++) {
		Elf e;
		if (elf_open(&e, dirfd, files[f]))
			continue;
		const Shdr *st = NULL;
		for (uint32_t k = 0; k < e.nsh; k++)
			if (e.sh[k].sh_type == SHT_SYMTAB)
				st = &e.sh[k];
		if (nf > 1)
			fprintf(out, "\n%s:\n", files[f]);
		if (!st) {
			fprintf(oerr, "%s: %s: no symbols\n", prog, files[f]);
			elf_close(&e);
			continue;
		}
		const Sym *ys = (const Sym *)(e.buf + st->sh_offset);
		const char *str = (const char *)e.buf + e.sh[st->sh_link].sh_offset;
		uint32_t n = st->sh_size / sizeof(Sym);
		NSym *v = xcalloc(n, sizeof *v);
		uint32_t m = 0;
		for (uint32_t i = 1; i < n; i++) {
			const Sym *y = &ys[i];
			uint8_t type = y->st_info & 15;
			const char *name = str + y->st_name;
			if (type == STT_FILE || type == STT_SECTION)
				continue;
			if (e.eh->e_machine == 183 && name[0] == '$' && (name[1] == 'd' || name[1] == 'x'))
				continue;
			if ((undef && y->st_shndx != SHN_UNDEF) || (defd && y->st_shndx == SHN_UNDEF) ||
			    (ext && (y->st_info >> 4) == STB_LOCAL))
				continue;
			v[m++] = (NSym){.name = name, .addr = y->st_value, .size = y->st_size,
					.type = nm_type(&e, y), .defined = y->st_shndx != SHN_UNDEF};
		}
		if (!nosort)
			qsort(v, m, sizeof *v, numeric ? ncmp_num : ncmp_name);
		/* one buffered pass */
		size_t cap = (size_t)m * 64 + 4096, len = 0;
		for (uint32_t i = 0; i < m; i++)
			cap += strlen(v[i].name);
		char *buf = malloc(cap);
		for (uint32_t i = 0; i < m; i++) {
			NSym *s = &v[i];
			if (s->defined)
				len += sprintf(buf + len, "%016llx ", (unsigned long long)s->addr);
			else
				len += sprintf(buf + len, "%16s ", "");
			if (size) {
				if (s->defined)
					len += sprintf(buf + len, "%016llx ", (unsigned long long)s->size);
				else
					len += sprintf(buf + len, "%16s ", "");
			}
			len += sprintf(buf + len, "%c %s\n", s->type, s->name);
		}
		fwrite(buf, 1, len, out);
		free(buf);
		free(v);
		elf_close(&e);
	}
	free(files);
	return oerrors ? 1 : 0;
}
