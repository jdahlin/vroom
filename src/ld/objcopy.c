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
#include "ld/ld.h"
#include "common/archive-reader.h"
#include "common/hash.h"
#include "common/scoped-cleanup.h"
#include "port/file.h"
#include "common/checked-arithmetic.h"
#include "elf-definitions.h"

#include <ctype.h>
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

#define OBJCOPY_NONE 0xffffffffu

static FILE *objcopy_error_stream;
static int objcopy_error_count;
static const char *objcopy_program_name = "vobjcopy";

static void objcopy_error_report(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void objcopy_error_report(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(objcopy_error_stream, "%s: error: ", objcopy_program_name);
	vfprintf(objcopy_error_stream, fmt, ap);
	fputc('\n', objcopy_error_stream);
	va_end(ap);
	objcopy_error_count++;
}

static void *objcopy_zeroed_alloc(size_t n, size_t s)
{
	void *p = calloc(n ? n : 1, s ? s : 1);
	if (!p) {
		fprintf(stderr, "%s: out of memory\n", objcopy_program_name);
		abort();
	}
	return p;
}

static uint64_t objcopy_value_align_up(uint64_t v, uint64_t a) { return a <= 1 ? v : (v + a - 1) / a * a; }
static uint64_t objcopy_value_align_skew(uint64_t v, uint64_t a, uint64_t skew)
{
	if (a <= 1)
		return v;
	skew %= a;
	return (v + a - 1 - skew) / a * a + skew;
}

/* ---------------- input ---------------- */

typedef struct {
	const uint8_t *file_bytes;
	size_t size;
	const elf_file_header *file_header;
	const elf_section_header *section_headers;
	uint32_t section_header_count;
	const elf_program_header *program_headers;
	uint32_t program_header_count;
	const char *section_name_table;
} objcopy_elf;

/* a table of n entries of sz bytes at off, inside the file and 8-aligned */
static bool objcopy_table_is_in_file(const objcopy_elf *e, uint64_t off, uint64_t n, uint64_t sz)
{
	return off % 8 == 0 && off <= e->size && n <= (e->size - off) / sz;
}

/* everything objcopy_elf_open's users index, checked once (as vlink's ld_object_is_valid) */
static bool objcopy_elf_is_valid(objcopy_elf *e)
{
	const elf_file_header *h = e->file_header;

	if ((h->e_shnum && h->e_shentsize != sizeof(elf_section_header)) || (h->e_phnum && h->e_phentsize != sizeof(elf_program_header)) ||
	    !objcopy_table_is_in_file(e, h->e_phoff, h->e_phnum, sizeof(elf_program_header)) || !objcopy_table_is_in_file(e, h->e_shoff, h->e_shnum, sizeof(elf_section_header)))
		return false;
	e->section_headers = (const elf_section_header *)(e->file_bytes + h->e_shoff);
	e->section_header_count = h->e_shnum;
	e->program_headers = (const elf_program_header *)(e->file_bytes + h->e_phoff);
	e->program_header_count = h->e_phnum;
	e->section_name_table = "";
	for (uint32_t k = 0; k < e->program_header_count; k++)
		if (e->program_headers[k].p_offset > e->size || e->program_headers[k].p_filesz > e->size - e->program_headers[k].p_offset)
			return false;
	if (!e->section_header_count)
		return h->e_shoff == 0 || h->e_shstrndx == 0;
	if (h->e_shstrndx >= e->section_header_count)
		return false;
	/* section 0 too: e_shstrndx and sh_link may name it, and its type is the file's to choose */
	for (uint32_t k = 0; k < e->section_header_count; k++) {
		const elf_section_header *s = &e->section_headers[k];

		if (s->sh_type != SHT_NOBITS && s->sh_type != SHT_NULL && !vchecked_range(s->sh_offset, s->sh_size, e->size))
			return false;
		if (s->sh_addralign & (s->sh_addralign - 1))
			return false;
	}
	const elf_section_header *ss = &e->section_headers[h->e_shstrndx];

	if (ss->sh_type != SHT_STRTAB || !ss->sh_size || e->file_bytes[ss->sh_offset + ss->sh_size - 1])
		return false;
	e->section_name_table = (const char *)e->file_bytes + ss->sh_offset;
	for (uint32_t k = 0; k < e->section_header_count; k++) {
		const elf_section_header *s = &e->section_headers[k];

		if (s->sh_name >= ss->sh_size)
			return false;
		if (s->sh_type == SHT_SYMTAB || s->sh_type == SHT_DYNSYM) {
			if (s->sh_offset % 8 || s->sh_size % sizeof(elf_symbol) || s->sh_link >= e->section_header_count)
				return false;
			const elf_section_header *st = &e->section_headers[s->sh_link];

			if (st->sh_type != SHT_STRTAB || !st->sh_size || e->file_bytes[st->sh_offset + st->sh_size - 1])
				return false;
			const elf_symbol *y = (const elf_symbol *)(e->file_bytes + s->sh_offset);

			for (uint64_t j = 0; j < s->sh_size / sizeof(elf_symbol); j++)
				if (y[j].st_name >= st->sh_size ||
				    (y[j].st_shndx && y[j].st_shndx < 0xff00 && y[j].st_shndx >= e->section_header_count))
					return false;
		}
		if (s->sh_type == SHT_RELA &&
		    (s->sh_offset % 8 || s->sh_size % sizeof(elf_rela) || s->sh_link >= e->section_header_count || s->sh_info >= e->section_header_count))
			return false;
	}
	return true;
}

static int objcopy_elf_open(objcopy_elf *e, int dirfd, const char *path)
{
	memset(e, 0, sizeof *e);
	VAUTOCLOSE int fd = openat(dirfd, path, O_RDONLY);
	if (fd < 0) {
		objcopy_error_report("'%s': %s", path, strerror(errno));
		return -1;
	}
	vfile_mapping m;
	struct stat st;
	bool mapped = vfile_map_fd(fd, &m, &st);
	e->size = m.n;
	e->file_bytes = m.p;
	if (!mapped || e->size < sizeof(elf_file_header) || memcmp(e->file_bytes, "\177ELF\2\1", 6)) {
		objcopy_error_report("'%s': not an ELF64 little-endian file", path);
		vfile_unmap(&m);
		e->file_bytes = NULL;
		return -1;
	}
	e->file_header = (const elf_file_header *)e->file_bytes;
	if (!objcopy_elf_is_valid(e)) {
		objcopy_error_report("'%s': invalid ELF file", path);
		vfile_unmap(&m);
		e->file_bytes = NULL;
		return -1;
	}
	return 0;
}
static void objcopy_elf_close(objcopy_elf *e)
{
	if (e->file_bytes)
		munmap((void *)e->file_bytes, e->size);
}

/* ---------------- StringTableBuilder (ELF, optimized) ---------------- */

typedef struct { const char *string; uint32_t length, hash; uint64_t offset; } objcopy_string_entry;
typedef struct {
	objcopy_string_entry *entries;
	uint32_t count, capacity;
	uint32_t *hash_table;			/* dedup hash: index+1 */
	uint32_t hash_mask;
	uint64_t size;
} objcopy_string_table;

static void objcopy_string_table_grow(objcopy_string_table *t)
{
	uint32_t nm = t->hash_table ? (t->hash_mask + 1) * 2 : 1024;
	uint32_t *nt = objcopy_zeroed_alloc(nm, 4);
	for (uint32_t i = 0; i < t->count; i++) {
		uint32_t j = t->entries[i].hash & (nm - 1);
		while (nt[j])
			j = (j + 1) & (nm - 1);
		nt[j] = i + 1;
	}
	free(t->hash_table);
	t->hash_table = nt;
	t->hash_mask = nm - 1;
}
static void objcopy_string_table_reserve(objcopy_string_table *t, uint32_t n)
{
	uint32_t m = 1024;
	while (m < n * 2 + 2)
		m *= 2;
	if (t->hash_table && t->hash_mask + 1 >= m)
		return;
	t->entries = realloc(t->entries, (t->capacity = n > t->capacity ? n : t->capacity) * sizeof(objcopy_string_entry));
	t->hash_mask = m / 2 - 1;	/* objcopy_string_table_grow doubles */
	uint32_t *old = t->hash_table;
	t->hash_table = objcopy_zeroed_alloc(1, 4);
	objcopy_string_table_grow(t);
	free(old);
}
static uint32_t objcopy_string_table_add(objcopy_string_table *t, const char *s)
{
	uint32_t len = strlen(s), h = vhash_string(s, len);
	if (!t->hash_table || (t->count + 1) * 2 > t->hash_mask)
		objcopy_string_table_grow(t);
	uint32_t j = h & t->hash_mask;
	while (t->hash_table[j]) {
		objcopy_string_entry *e = &t->entries[t->hash_table[j] - 1];
		if (e->hash == h && e->length == len && !memcmp(e->string, s, len))
			return t->hash_table[j] - 1;
		j = (j + 1) & t->hash_mask;
	}
	if (t->count == t->capacity) {
		t->capacity = t->capacity ? t->capacity * 2 : 256;
		t->entries = realloc(t->entries, t->capacity * sizeof(objcopy_string_entry));
	}
	t->entries[t->count] = (objcopy_string_entry){.string = s, .length = len, .hash = h};
	t->hash_table[j] = ++t->count;
	return t->count - 1;
}
static uint64_t objcopy_string_table_find(const objcopy_string_table *t, const char *s)
{
	uint32_t len = strlen(s), h = vhash_string(s, len);
	if (!len)
		return 0;
	uint32_t j = h & t->hash_mask;
	while (t->hash_table[j]) {
		const objcopy_string_entry *e = &t->entries[t->hash_table[j] - 1];
		if (e->hash == h && e->length == len && !memcmp(e->string, s, len))
			return e->offset;
		j = (j + 1) & t->hash_mask;
	}
	return 0;
}
/* multikeySort order: by characters from the end, larger first, a string
   before its own suffixes; a total order on distinct strings.  That is
   descending lexicographic order of the reversed strings, so each entry
   carries its reversed name (one shared buffer) and comparisons are
   memcmp, with the first 8 bytes as an integer key. */
typedef struct { uint64_t key; const char *reversed_name; uint32_t length; objcopy_string_entry *entry; } objcopy_sort_entry;
static int objcopy_string_table_cmp(const void *a, const void *b)
{
	const objcopy_sort_entry *p = a, *q = b;
	if (p->key != q->key)
		return p->key < q->key ? 1 : -1;
	uint32_t n = p->length < q->length ? p->length : q->length;
	if (n > 8) {
		int c = memcmp(p->reversed_name + 8, q->reversed_name + 8, n - 8);
		if (c)
			return -c;
	}
	return (p->length < q->length) - (p->length > q->length);
}
static void objcopy_string_table_finalize(objcopy_string_table *t)
{
	objcopy_sort_entry *p = objcopy_zeroed_alloc(t->count, sizeof *p);
	size_t tot = 0;
	for (uint32_t i = 0; i < t->count; i++)
		tot += t->entries[i].length;
	char *rev = objcopy_zeroed_alloc(tot + 8, 1);
	size_t ro = 0;
	for (uint32_t i = 0; i < t->count; i++) {
		objcopy_string_entry *e = &t->entries[i];
		char *r = rev + ro;
		for (uint32_t j = 0; j < e->length; j++)
			r[j] = e->string[e->length - 1 - j];
		uint64_t k = 0;
		for (uint32_t j = 0; j < 8; j++)
			k = k << 8 | (j < e->length ? (uint8_t)r[j] : 0);
		p[i] = (objcopy_sort_entry){k, r, e->length, e};
		ro += e->length;
	}
	/* LSD radix sort on the key, descending; ties (same last 8 bytes)
	   are then sorted by the full comparison */
	objcopy_sort_entry *tmp = objcopy_zeroed_alloc(t->count, sizeof *tmp);
	for (int sh = 0; sh < 64; sh += 8) {
		uint32_t cnt[257] = {0};
		for (uint32_t i = 0; i < t->count; i++)
			cnt[255 - (p[i].key >> sh & 255) + 1]++;
		bool one = false;
		for (int i = 1; i < 257; i++)
			one |= cnt[i] == t->count;
		if (one)
			continue;
		for (int i = 0; i < 256; i++)
			cnt[i + 1] += cnt[i];
		for (uint32_t i = 0; i < t->count; i++)
			tmp[cnt[255 - (p[i].key >> sh & 255)]++] = p[i];
		objcopy_sort_entry *x = p;
		p = tmp;
		tmp = x;
	}
	free(tmp);
	for (uint32_t i = 0, j; i < t->count; i = j) {
		for (j = i + 1; j < t->count && p[j].key == p[i].key; j++)
			;
		if (j - i > 1 && p[i].length >= 8)
			qsort(p + i, j - i, sizeof *p, objcopy_string_table_cmp);
	}
	t->size = 1;
	const objcopy_string_entry *prev = NULL;
	for (uint32_t i = 0; i < t->count; i++) {
		objcopy_string_entry *e = p[i].entry;
		if (prev && prev->length >= e->length && !memcmp(prev->string + prev->length - e->length, e->string, e->length)) {
			e->offset = t->size - e->length - 1;
			continue;
		}
		e->offset = t->size;
		t->size += e->length + 1;
		prev = e;
	}
	free(p);
	free(rev);
}
static void objcopy_string_table_write(const objcopy_string_table *t, uint8_t *dst)
{
	memset(dst, 0, t->size);
	for (uint32_t i = 0; i < t->count; i++)
		memcpy(dst + t->entries[i].offset, t->entries[i].string, t->entries[i].length);
}
static void objcopy_string_table_free(objcopy_string_table *t)
{
	free(t->entries);
	free(t->hash_table);
}

/* ---------------- objcopy model ---------------- */

enum { OBJCOPY_SECTION_KIND_RAW, OBJCOPY_SECTION_KIND_SYMBOL_TABLE, OBJCOPY_SECTION_KIND_STRING_TABLE, OBJCOPY_SECTION_KIND_RELA };

typedef struct {
	const char *name;
	char *new_name;
	const char *original_name;		/* the input's name, when --rename-section changed it */
	uint32_t type, link, info;
	uint64_t flags, address, original_offset, size, alignment, entry_size;
	const uint8_t *data;
	int parent_segment;			/* segment or -1 */
	bool removed;
	uint8_t kind;
	uint32_t new_index;			/* new index */
	uint64_t new_offset;			/* new offset */
	uint8_t *generated_contents;			/* generated contents */
} objcopy_section;

/* --set-section-flags/--rename-section flags (llvm-objcopy's getNewShfFlags; NOBITS
 * with contents or load becomes PROGBITS) */
static void objcopy_section_flags_set(objcopy_section *s, const char *f)
{
	const uint64_t mask = SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR | SHF_MERGE | SHF_STRINGS | 0x80000000u;
	uint64_t nf = SHF_WRITE;
	bool contents = false, load = false;

	while (*f) {
		size_t l = strcspn(f, ",");
#define OBJCOPY_FLAG_IS(x) (l == sizeof(x) - 1 && !strncmp(f, x, l))
		if (OBJCOPY_FLAG_IS("alloc")) nf |= SHF_ALLOC;
		else if (OBJCOPY_FLAG_IS("readonly")) nf &= ~(uint64_t)SHF_WRITE;
		else if (OBJCOPY_FLAG_IS("code")) nf |= SHF_EXECINSTR;
		else if (OBJCOPY_FLAG_IS("merge")) nf |= SHF_MERGE;
		else if (OBJCOPY_FLAG_IS("strings")) nf |= SHF_STRINGS;
		else if (OBJCOPY_FLAG_IS("exclude")) nf |= 0x80000000u;
		else if (OBJCOPY_FLAG_IS("contents")) contents = true;
		else if (OBJCOPY_FLAG_IS("load")) load = true;
		else if (!(OBJCOPY_FLAG_IS("data") || OBJCOPY_FLAG_IS("rom") || OBJCOPY_FLAG_IS("share") || OBJCOPY_FLAG_IS("debug") || OBJCOPY_FLAG_IS("large") || OBJCOPY_FLAG_IS("noload")))
			objcopy_error_report("unrecognized section flag '%.*s'", (int)l, f);
#undef OBJCOPY_FLAG_IS
		f += l + (f[l] == ',');
	}
	s->flags = (s->flags & ~mask) | nf;
	if (s->type == SHT_NOBITS && (!(s->flags & SHF_ALLOC) || contents || load)) {
		s->type = SHT_PROGBITS;	/* (llvm: zero-filled contents) */
		s->generated_contents = objcopy_zeroed_alloc(s->size + 1, 1);
	}
}

typedef struct {
	elf_program_header program_header;
	uint64_t original_offset, offset;
	uint64_t file_size, virtual_address, alignment;
	uint32_t index;
	int parent_segment;
} objcopy_segment;

typedef struct {
	const char *name;
	char *new_name;
	uint8_t info, other;
	uint16_t original_section_index;			/* original */
	uint64_t value, size;
	bool removed;
	uint32_t new_index, string_index;		/* new index, string table entry */
} objcopy_symbol;

typedef struct {
	const char *text;
} objcopy_pattern;

static struct objcopy_option_set {
	const char *input_path, *output_path;
	bool binary, binary_input, strip_all, strip_debug, keep_debug, wildcard;
	const char *prefix_symbols, *prefix_alloc_sections;
	const char **remove_names;
	uint32_t remove_count;
	const char **redefine_symbols;	/* "old=new" */
	uint32_t redefine_count;
	const char **add_sections;	/* --add-section name=file */
	uint32_t add_count;
	const char *debuglink;
	const char **set_section_flags;	/* --set-section-flags name=f1,f2 */
	uint32_t set_section_flags_count;
	const char **rename_sections;	/* --rename-section old=new[,f1,f2] */
	uint32_t rename_sections_count;
	const char **only_sections;
	uint32_t only_count;
	const char **unneeded_symbols;
	uint32_t unneeded_count;
	const char **dump_sections;
	uint32_t dump_count;
} objcopy_options;

static bool objcopy_name_list_match(const char **v, uint32_t n, const char *name)
{
	for (uint32_t i = 0; i < n; i++) {
		const char *p = v[i];
		bool neg = false;
		if (objcopy_options.wildcard && *p == '!') {
			neg = true;
			p++;
		}
		bool m;
		size_t meta = 0;
		while (p[meta] && !(objcopy_options.wildcard && (p[meta] == '*' || p[meta] == '?' || p[meta] == '[' || p[meta] == '\\')))
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

static const char *objcopy_section_name(objcopy_section *s) { return s->new_name ? s->new_name : s->name; }
static const char *objcopy_symbol_name(objcopy_symbol *y) { return y->new_name ? y->new_name : y->name; }

static bool objcopy_section_is_in_segment(const objcopy_section *s, const objcopy_segment *g)
{
	uint64_t sz = s->size ? s->size : 1;
	if (s->type == SHT_NOBITS) {
		if (!(s->flags & SHF_ALLOC))
			return false;
		if (((s->flags & SHF_TLS) != 0) != (g->program_header.p_type == 7))
			return false;
		return g->program_header.p_vaddr <= s->address && g->program_header.p_vaddr + g->program_header.p_memsz >= s->address + sz;
	}
	return g->original_offset <= s->original_offset && g->original_offset + g->file_size >= s->original_offset + sz;
}
static bool objcopy_segment_is_before(const objcopy_segment *a, const objcopy_segment *b)
{
	if (a->original_offset != b->original_offset)
		return a->original_offset < b->original_offset;
	return a->index < b->index;
}

static int objcopy_file_write(int dirfd, const char *path, const uint8_t *b, uint64_t n, int mode)
{
	if (!strcmp(path, "/dev/null"))
		return 0;
	char tmp[4096];
	snprintf(tmp, sizeof tmp, "%s.vobjcopy-tmp", path);
	VAUTOCLOSE int fd = openat(dirfd, tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0) {
		objcopy_error_report("'%s': %s", path, strerror(errno));
		return 1;
	}
	uint64_t off = 0;
	while (off < n) {
		ssize_t w = write(fd, b + off, n - off > (1u << 30) ? (1u << 30) : n - off);
		if (w <= 0) {
			objcopy_error_report("write '%s': %s", path, strerror(errno));
			return 1;
		}
		off += w;
	}
	/* rename over the target: the input may be the output */
	if (renameat(dirfd, tmp, dirfd, path)) {
		objcopy_error_report("rename '%s': %s", path, strerror(errno));
		return 1;
	}
	return 0;
}

/* -I binary: the file as .data with _binary_<name>_start/_end/_size, laid out as llvm-objcopy */
static int objcopy_binary_input(int dirfd)
{
	VAUTOCLOSE int fd = openat(dirfd, objcopy_options.input_path, O_RDONLY);
	struct stat st;

	if (fd < 0 || fstat(fd, &st) < 0) {
		objcopy_error_report("'%s': %s", objcopy_options.input_path, strerror(errno));
		return 1;
	}
	size_t dsz = (size_t)st.st_size;
	VAUTOFREE uint8_t *data = objcopy_zeroed_alloc(dsz + 1, 1);
	if (dsz && read(fd, data, dsz) != (ssize_t)dsz)
		objcopy_error_report("'%s': short read", objcopy_options.input_path);
	char base[4096], nm[3][4200];
	size_t bl = 0;
	for (const char *c = objcopy_options.input_path; *c && bl + 1 < sizeof base; c++)
		base[bl++] = isalnum((unsigned char)*c) ? *c : '_';
	base[bl] = 0;
	static const char *const suf[3] = { "start", "end", "size" };
	VCLEANUP(objcopy_string_table_free) objcopy_string_table t = {0};
	for (int i = 0; i < 3; i++) {
		snprintf(nm[i], sizeof nm[i], "_binary_%s_%s", base, suf[i]);
		objcopy_string_table_add(&t, nm[i]);
	}
	objcopy_string_table_add(&t, ".strtab");
	objcopy_string_table_add(&t, ".symtab");
	objcopy_string_table_add(&t, ".data");
	objcopy_string_table_finalize(&t);
	uint64_t stroff = sizeof(elf_file_header), symoff = objcopy_value_align_up(stroff + t.size, 8), datoff = symoff + 4 * sizeof(elf_symbol);
	uint64_t shoff = objcopy_value_align_up(datoff + dsz, 8), total = shoff + 4 * sizeof(elf_section_header);
	VAUTOFREE uint8_t *b = objcopy_zeroed_alloc(total, 1);
	elf_file_header *h = (elf_file_header *)b;
	memcpy(h->e_ident, "\177ELF\2\1\1", 7);
	h->e_type = 1;
	h->e_machine = 183;
	h->e_version = 1;
	h->e_ehsize = sizeof(elf_file_header);
	h->e_shentsize = sizeof(elf_section_header);
	h->e_shoff = shoff;
	h->e_shnum = 4;
	h->e_shstrndx = 1;
	objcopy_string_table_write(&t, b + stroff);
	elf_symbol *y = (elf_symbol *)(b + symoff);
	uint64_t val[3] = { 0, dsz, dsz };
	for (int i = 0; i < 3; i++)
		y[i + 1] = (elf_symbol){.st_name = (uint32_t)objcopy_string_table_find(&t, nm[i]), .st_info = STB_GLOBAL << 4,
				 .st_shndx = i == 2 ? SHN_ABS : 3, .st_value = val[i]};
	memcpy(b + datoff, data, dsz);
	elf_section_header *sh = (elf_section_header *)(b + shoff);
	sh[1] = (elf_section_header){.sh_name = (uint32_t)objcopy_string_table_find(&t, ".strtab"), .sh_type = SHT_STRTAB, .sh_offset = stroff,
		       .sh_size = t.size, .sh_addralign = 1};
	sh[2] = (elf_section_header){.sh_name = (uint32_t)objcopy_string_table_find(&t, ".symtab"), .sh_type = SHT_SYMTAB, .sh_offset = symoff,
		       .sh_size = 4 * sizeof(elf_symbol), .sh_link = 1, .sh_info = 1, .sh_addralign = 8, .sh_entsize = sizeof(elf_symbol)};
	sh[3] = (elf_section_header){.sh_name = (uint32_t)objcopy_string_table_find(&t, ".data"), .sh_type = SHT_PROGBITS, .sh_flags = SHF_WRITE | SHF_ALLOC,
		       .sh_offset = datoff, .sh_size = dsz, .sh_addralign = 1};
	return objcopy_error_count ? 1 : objcopy_file_write(dirfd, objcopy_options.output_path, b, total, 0666);
}

/* what objcopy_run owns besides plain allocations: the generated contents of
   sections and the new names of symbols (the arrays are zeroed, so the whole
   capacity can be walked) */
typedef struct {
	objcopy_section *sections;
	uint32_t section_count;
	objcopy_symbol *symbols;
	uint32_t symbol_count;
} objcopy_owned;

static void objcopy_owned_free(objcopy_owned *o)
{
	for (uint32_t k = 0; k < o->section_count; k++) {
		free(o->sections[k].generated_contents);
		free(o->sections[k].new_name);
	}
	for (uint32_t i = 0; i < o->symbol_count; i++)
		free(o->symbols[i].new_name);
	free(o->sections);
	free(o->symbols);
}

static int objcopy_run(int dirfd)
{
	VCLEANUP(objcopy_elf_close) objcopy_elf e;
	if (objcopy_elf_open(&e, dirfd, objcopy_options.input_path))
		return 1;
	const elf_file_header *eh = e.file_header;
	bool rel = eh->e_type == 1;
	uint32_t ns = e.section_header_count;
	VCLEANUP(objcopy_owned_free) objcopy_owned own = { 0 };
	objcopy_section *sec = own.sections = objcopy_zeroed_alloc(ns + objcopy_options.add_count + 1, sizeof *sec);
	own.section_count = ns + objcopy_options.add_count + 1;
	for (uint32_t i = 0; i < ns; i++) {
		const elf_section_header *h = &e.section_headers[i];
		objcopy_section *s = &sec[i];
		s->name = e.section_name_table + h->sh_name;
		s->type = h->sh_type;
		s->flags = h->sh_flags;
		s->address = h->sh_addr;
		s->original_offset = h->sh_offset;
		s->size = h->sh_size;
		s->link = h->sh_link;
		s->info = h->sh_info;
		s->alignment = h->sh_addralign;
		s->entry_size = h->sh_entsize;
		s->data = h->sh_type == SHT_NOBITS ? NULL : e.file_bytes + h->sh_offset;
		s->parent_segment = -1;
		if (h->sh_type == SHT_SYMTAB)
			s->kind = OBJCOPY_SECTION_KIND_SYMBOL_TABLE;
		else if (h->sh_type == SHT_STRTAB && !(h->sh_flags & SHF_ALLOC))
			s->kind = OBJCOPY_SECTION_KIND_STRING_TABLE;
		else if (h->sh_type == SHT_RELA && h->sh_link && e.section_headers[h->sh_link].sh_type == SHT_SYMTAB)
			s->kind = OBJCOPY_SECTION_KIND_RELA;
	}
	if (!rel && e.section_headers[0].sh_size)
		objcopy_error_report("extended section numbering not supported");
	/* --only-keep-debug (relocatable objects): allocated sections other than notes keep only their headers */
	if (objcopy_options.keep_debug) {
		if (!rel)
			objcopy_error_report("--only-keep-debug: only relocatable objects are supported");
		for (uint32_t k = 1; k < ns; k++)
			if ((sec[k].flags & SHF_ALLOC) && sec[k].type != SHT_NOTE)
				sec[k].type = SHT_NOBITS;
	}
	/* --rename-section (llvm-objcopy: before --set-section-flags, which sees the new name;
	 * the .rela of a renamed section follows it; flags as --set-section-flags) */
	for (uint32_t a = 0; a < objcopy_options.rename_sections_count; a++) {
		const char *eq = strchr(objcopy_options.rename_sections[a], '='), *fl = strchr(eq + 1, ',');
		size_t ol = (size_t)(eq - objcopy_options.rename_sections[a]), nl = fl ? (size_t)(fl - eq - 1) : strlen(eq + 1);
		for (uint32_t k = 1; k < ns; k++) {
			objcopy_section *s = &sec[k];
			if (s->original_name || strlen(s->name) != ol || strncmp(s->name, objcopy_options.rename_sections[a], ol))
				continue;
			s->original_name = s->name;
			s->new_name = strndup(eq + 1, nl);
			s->name = s->new_name;
			if (fl)
				objcopy_section_flags_set(s, fl + 1);
			for (uint32_t r = 1; r < ns; r++)
				if (sec[r].kind == OBJCOPY_SECTION_KIND_RELA && sec[r].info == k && !(sec[r].flags & SHF_ALLOC) && !sec[r].original_name) {
					size_t n = nl + 6;
					sec[r].original_name = sec[r].name;
					sec[r].new_name = objcopy_zeroed_alloc(n, 1);
					snprintf(sec[r].new_name, n, ".rela%s", s->name);
					sec[r].name = sec[r].new_name;
				}
		}
	}
	/* --set-section-flags */
	for (uint32_t a = 0; a < objcopy_options.set_section_flags_count; a++) {
		const char *eq = strchr(objcopy_options.set_section_flags[a], '=');
		size_t nl = (size_t)(eq - objcopy_options.set_section_flags[a]);
		for (uint32_t k = 1; k < ns; k++)
			if (strlen(sec[k].name) == nl && !strncmp(sec[k].name, objcopy_options.set_section_flags[a], nl))
				objcopy_section_flags_set(&sec[k], eq + 1);
	}
	/* --add-section: at the end, after everything else (llvm-objcopy: PROGBITS, or NOTE for .note*) */
	for (uint32_t a = 0; a < objcopy_options.add_count; a++) {
		const char *eq = strchr(objcopy_options.add_sections[a], '=');
		objcopy_section *s = &sec[ns];
		struct stat st;
		VAUTOCLOSE int fd = openat(dirfd, eq + 1, O_RDONLY);

		if (fd < 0 || fstat(fd, &st) < 0) {
			objcopy_error_report("'%s': %s", eq + 1, strerror(errno));
			continue;
		}
		s->new_name = strndup(objcopy_options.add_sections[a], (size_t)(eq - objcopy_options.add_sections[a]));
		s->name = s->new_name;
		s->type = !strncmp(s->name, ".note", 5) && strcmp(s->name, ".note.GNU-stack") ? SHT_NOTE : SHT_PROGBITS;
		s->alignment = 1;
		s->size = (uint64_t)st.st_size;
		s->original_offset = UINT64_MAX;
		s->parent_segment = -1;
		s->generated_contents = objcopy_zeroed_alloc(s->size + 1, 1);
		if (s->size && read(fd, s->generated_contents, s->size) != (ssize_t)s->size)
			objcopy_error_report("'%s': short read", eq + 1);
		if (s->type == SHT_NOTE && s->size && s->size < 12)
			objcopy_error_report("'%s': %s data must be either empty or at least 12 bytes long", objcopy_options.input_path, s->name);
		ns++;
	}
	/* --add-gnu-debuglink (after --add-section): .gnu_debuglink = basename, NUL, padding to 4, CRC-32 of the file */
	if (objcopy_options.debuglink) {
		objcopy_section *s = &sec[ns];
		VAUTOCLOSE int fd = openat(dirfd, objcopy_options.debuglink, O_RDONLY);
		struct stat st;
		const char *base = strrchr(objcopy_options.debuglink, '/') ? strrchr(objcopy_options.debuglink, '/') + 1 : objcopy_options.debuglink;
		size_t bl = strlen(base), sz = (bl + 4) / 4 * 4 + 4;
		uint32_t crc = 0xffffffff;

		if (fd < 0 || fstat(fd, &st) < 0) {
			objcopy_error_report("'%s': %s", objcopy_options.debuglink, strerror(errno));
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
			s->alignment = 4;
			s->size = sz;
			s->original_offset = UINT64_MAX;
			s->parent_segment = -1;
			s->generated_contents = objcopy_zeroed_alloc(sz, 1);
			memcpy(s->generated_contents, base, bl);
			memcpy(s->generated_contents + sz - 4, &crc, 4);
			ns++;
		}
	}

	/* segments: real ones, then the ELF header and program header "segments" */
	uint32_t ng = e.program_header_count + 2;
	VAUTOFREE objcopy_segment *seg = objcopy_zeroed_alloc(ng, sizeof *seg);
	for (uint32_t i = 0; i < e.program_header_count; i++) {
		seg[i].program_header = e.program_headers[i];
		seg[i].original_offset = seg[i].offset = e.program_headers[i].p_offset;
		seg[i].file_size = e.program_headers[i].p_filesz;
		seg[i].virtual_address = e.program_headers[i].p_vaddr;
		seg[i].alignment = e.program_headers[i].p_align;
		seg[i].index = i;
		seg[i].parent_segment = -1;
		for (uint32_t k = 1; k < ns; k++)
			if (objcopy_section_is_in_segment(&sec[k], &seg[i]))
				if (sec[k].parent_segment < 0 || seg[sec[k].parent_segment].original_offset > seg[i].original_offset)
					sec[k].parent_segment = i;
	}
	uint32_t ehs = e.program_header_count, phs = e.program_header_count + 1;
	seg[ehs] = (objcopy_segment){.original_offset = 0, .file_size = sizeof(elf_file_header), .index = ehs, .parent_segment = -1};
	seg[phs] = (objcopy_segment){.original_offset = eh->e_phoff, .virtual_address = eh->e_phoff, .file_size = (uint64_t)eh->e_phentsize * eh->e_phnum,
			  .alignment = 8, .index = phs, .parent_segment = -1};
	for (uint32_t c = 0; c < ng; c++) {
		objcopy_segment *ch = &seg[c];
		for (uint32_t p = 0; p < e.program_header_count; p++) {
			objcopy_segment *pa = &seg[p];
			if (p == c || !(pa->original_offset <= ch->original_offset && pa->original_offset + pa->file_size > ch->original_offset))
				continue;
			if (objcopy_segment_is_before(pa, ch) && (ch->parent_segment < 0 || objcopy_segment_is_before(pa, &seg[ch->parent_segment])))
				ch->parent_segment = p;
		}
	}

	/* --dump-section */
	for (uint32_t i = 0; i < objcopy_options.dump_count; i++) {
		char *eq = strchr(objcopy_options.dump_sections[i], '=');
		if (!eq) {
			objcopy_error_report("bad --dump-section %s", objcopy_options.dump_sections[i]);
			continue;
		}
		size_t nl = eq - objcopy_options.dump_sections[i];
		uint32_t k;
		for (k = 1; k < ns; k++)
			if (strlen(sec[k].name) == nl && !memcmp(sec[k].name, objcopy_options.dump_sections[i], nl))
				break;
		if (k == ns) {
			objcopy_error_report("section '%.*s' not found", (int)nl, objcopy_options.dump_sections[i]);
			continue;
		}
		if (sec[k].type == SHT_NOBITS) {
			objcopy_error_report("cannot dump section '%s': it has no contents", sec[k].name);
			continue;
		}
		objcopy_file_write(dirfd, eq + 1, sec[k].data, sec[k].size, 0666);
	}

	/* section removal (replaceAndRemoveSections) */
	uint32_t shstr_i = eh->e_shstrndx;
	uint32_t symtab_i = OBJCOPY_NONE;
	for (uint32_t k = 1; k < ns; k++)
		if (sec[k].kind == OBJCOPY_SECTION_KIND_SYMBOL_TABLE)
			symtab_i = k;
	uint32_t symstr_i = symtab_i != OBJCOPY_NONE ? sec[symtab_i].link : OBJCOPY_NONE;
	for (uint32_t k = 1; k < ns; k++) {
		objcopy_section *s = &sec[k];
		bool rm = objcopy_options.remove_count && objcopy_name_list_match(objcopy_options.remove_names, objcopy_options.remove_count, s->original_name ? s->original_name : s->name);
		if (objcopy_options.strip_all && !rm && k != shstr_i && strncmp(s->name, ".gnu.warning", 12) &&
		    strncmp(s->name, ".gnu_debuglink", 14) && s->type != 0x70000003 && s->parent_segment < 0)
			rm = !(s->flags & SHF_ALLOC);
		if (objcopy_options.strip_debug && !(s->flags & SHF_ALLOC) &&
		    (!strncmp(s->name, ".debug", 6) || !strncmp(s->name, ".zdebug", 7) || !strcmp(s->name, ".gdb_index")))
			rm = true;	/* llvm-objcopy's isDebugSection */
		if (objcopy_options.only_count) {
			if (objcopy_name_list_match(objcopy_options.only_sections, objcopy_options.only_count, s->name))
				rm = false;
			else if (!rm)
				rm = !(k == shstr_i || k == symtab_i || k == symstr_i);
		}
		s->removed = rm;
	}
	for (uint32_t k = 1; k < ns; k++) {
		objcopy_section *s = &sec[k];
		if (!s->removed && (s->type == SHT_RELA || s->type == SHT_REL) && (s->flags & SHF_INFO_LINK || s->kind == OBJCOPY_SECTION_KIND_RELA) &&
		    s->info && s->info < ns && sec[s->info].removed)
			s->removed = true;
	}
	if (symtab_i != OBJCOPY_NONE && sec[symtab_i].removed)
		symtab_i = OBJCOPY_NONE;
	if (sec[shstr_i].removed) {
		objcopy_error_report("cannot remove the section header string table");
		return 1;
	}

	/* symbols */
	objcopy_symbol *sym = NULL;
	uint32_t nsym = 0;
	if (symtab_i != OBJCOPY_NONE) {
		objcopy_section *st = &sec[symtab_i];
		const elf_symbol *ys = (const elf_symbol *)st->data;
		const char *str = (const char *)e.file_bytes + e.section_headers[st->link].sh_offset;
		nsym = st->size / sizeof(elf_symbol);
		sym = own.symbols = objcopy_zeroed_alloc(nsym, sizeof *sym);
		own.symbol_count = nsym;
		for (uint32_t i = 0; i < nsym; i++) {
			sym[i].name = str + ys[i].st_name;
			sym[i].info = ys[i].st_info;
			sym[i].other = ys[i].st_other;
			sym[i].original_section_index = ys[i].st_shndx;
			sym[i].value = ys[i].st_value;
			sym[i].size = ys[i].st_size;
			if (i && sym[i].original_section_index && sym[i].original_section_index < 0xff00 && sym[i].original_section_index < ns && sec[sym[i].original_section_index].removed)
				sym[i].removed = true;
		}
		VAUTOFREE bool *referenced = NULL;
		/* llvm-objcopy's Symbol::Referenced: named by a kept relocation or group */
		if ((objcopy_options.unneeded_count && rel) || objcopy_options.only_count) {
			referenced = objcopy_zeroed_alloc(nsym, 1);
			for (uint32_t k = 1; k < ns; k++) {
				if (!sec[k].removed && sec[k].type == SHT_GROUP && sec[k].info < nsym)
					referenced[sec[k].info] = true;
				if (sec[k].removed || sec[k].kind != OBJCOPY_SECTION_KIND_RELA)
					continue;
				const elf_rela *r = (const elf_rela *)sec[k].data;
				for (uint64_t j = 0; j < sec[k].size / sizeof(elf_rela); j++)
					if ((r[j].r_info >> 32) < nsym)
						referenced[r[j].r_info >> 32] = true;
			}
		}
		for (uint32_t i = 1; i < nsym; i++) {
			objcopy_symbol *y = &sym[i];
			uint8_t type = y->info & 15, bind = y->info >> 4;
			for (uint32_t r = 0; r < objcopy_options.redefine_count && type != STT_SECTION; r++) {
				const char *eq = strchr(objcopy_options.redefine_symbols[r], '=');

				if ((size_t)(eq - objcopy_options.redefine_symbols[r]) == strlen(y->name) && !strncmp(y->name, objcopy_options.redefine_symbols[r], eq - objcopy_options.redefine_symbols[r])) {
					free(y->new_name);
					y->new_name = strdup(eq + 1);
					break;
				}
			}
			if (objcopy_options.prefix_symbols && type != STT_SECTION) {
				const char *nm = y->new_name ? y->new_name : y->name;
				size_t a = strlen(objcopy_options.prefix_symbols), b = strlen(nm);
				char *old = y->new_name;
				y->new_name = objcopy_zeroed_alloc(a + b + 1, 1);
				memcpy(y->new_name, objcopy_options.prefix_symbols, a);
				memcpy(y->new_name + a, nm, b + 1);
				free(old);
			}
			if (objcopy_options.strip_all || (objcopy_options.strip_debug && type == STT_FILE))
				y->removed = true;
			else if (objcopy_options.unneeded_count && objcopy_name_list_match(objcopy_options.unneeded_symbols, objcopy_options.unneeded_count, objcopy_symbol_name(y))) {
				bool unneeded = !referenced || (!referenced[i] && (bind == STB_LOCAL || y->original_section_index == SHN_UNDEF) &&
								type != STT_FILE && type != STT_SECTION);
				if (!rel || unneeded)
					y->removed = true;
			}
			/* -j: undefined symbols whose references were all removed go too */
			if (objcopy_options.only_count && y->original_section_index == SHN_UNDEF && !referenced[i])
				y->removed = true;
		}
		uint32_t idx = 0;
		for (uint32_t i = 0; i < nsym; i++)
			if (!sym[i].removed)
				sym[i].new_index = idx++;
		/* a removed symbol must not be referenced by a kept relocation */
		for (uint32_t k = 1; k < ns; k++) {
			if (sec[k].removed || sec[k].kind != OBJCOPY_SECTION_KIND_RELA)
				continue;
			const elf_rela *r = (const elf_rela *)sec[k].data;
			for (uint64_t j = 0; j < sec[k].size / sizeof(elf_rela); j++) {
				uint32_t si = r[j].r_info >> 32;
				if (si && (si >= nsym || sym[si].removed)) {
					objcopy_error_report("not stripping symbol '%s' because it is named in a relocation",
					       si < nsym ? sym[si].name : "?");
					return 1;
				}
			}
		}
	}

	/* --prefix-alloc-sections */
	if (objcopy_options.prefix_alloc_sections) {
		size_t pl = strlen(objcopy_options.prefix_alloc_sections);
		for (uint32_t k = 1; k < ns; k++) {
			objcopy_section *s = &sec[k];
			if (s->removed)
				continue;
			const char *base = NULL, *rp = NULL;
			if (s->flags & SHF_ALLOC) {
				base = s->name;
				rp = "";
			} else if (s->kind == OBJCOPY_SECTION_KIND_RELA && s->info && (sec[s->info].flags & SHF_ALLOC)) {
				base = sec[s->info].name;
				rp = ".rela";
			}
			if (!base)
				continue;
			size_t n = strlen(rp) + pl + strlen(base) + 1;
			s->new_name = objcopy_zeroed_alloc(n, 1);
			snprintf(s->new_name, n, "%s%s%s", rp, objcopy_options.prefix_alloc_sections, base);
		}
	}

	if (objcopy_options.binary) {
		/* BinaryWriter: alloc sections with contents at LMA - min LMA */
		uint64_t min = UINT64_MAX, total = 0;
		VAUTOFREE uint64_t *lma = objcopy_zeroed_alloc(ns, 8);
		for (uint32_t k = 1; k < ns; k++) {
			objcopy_section *s = &sec[k];
			if (s->removed || !(s->flags & SHF_ALLOC))
				continue;
			lma[k] = s->address;
			if (s->parent_segment >= 0)
				lma[k] = s->original_offset - seg[s->parent_segment].original_offset + seg[s->parent_segment].program_header.p_paddr;
			if (s->type != SHT_NOBITS && s->size && lma[k] < min)
				min = lma[k];
		}
		for (uint32_t k = 1; k < ns; k++) {
			objcopy_section *s = &sec[k];
			if (s->removed || !(s->flags & SHF_ALLOC) || s->type == SHT_NOBITS || !s->size)
				continue;
			if (lma[k] - min + s->size > total)
				total = lma[k] - min + s->size;
		}
		uint8_t *b = total ? mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0) : NULL;
		for (uint32_t k = 1; k < ns; k++) {
			objcopy_section *s = &sec[k];
			if (s->removed || !(s->flags & SHF_ALLOC) || s->type == SHT_NOBITS || !s->size)
				continue;
			memcpy(b + lma[k] - min, s->data, s->size);
		}
		int rc = objcopy_error_count ? 1 : objcopy_file_write(dirfd, objcopy_options.output_path, b, total, 0777);
		if (b)
			munmap(b, total);
		return rc;
	}

	/* indices of kept sections */
	uint32_t nkept = 0;
	for (uint32_t k = 1; k < ns; k++)
		if (!sec[k].removed)
			sec[k].new_index = ++nkept;
	/* string tables */
	VCLEANUP(objcopy_string_table_free) objcopy_string_table names = {0}, symnames = {0};
	bool shared_str = symtab_i != OBJCOPY_NONE && sec[symtab_i].link == shstr_i;
	for (uint32_t k = 1; k < ns; k++)
		if (!sec[k].removed)
			objcopy_string_table_add(&names, objcopy_section_name(&sec[k]));
	objcopy_string_table *sn = shared_str ? &names : &symnames;
	objcopy_string_table_reserve(sn, nsym + (shared_str ? ns : 0));
	if (symtab_i != OBJCOPY_NONE)
		for (uint32_t i = 0; i < nsym; i++)
			if (!sym[i].removed)
				sym[i].string_index = objcopy_string_table_add(sn, objcopy_symbol_name(&sym[i]));
	objcopy_string_table_finalize(&names);
	if (!shared_str && symtab_i != OBJCOPY_NONE)
		objcopy_string_table_finalize(&symnames);
	/* generated sections */
	uint32_t nsymout = 0, maxlocal = 0;
	for (uint32_t i = 0; i < nsym; i++)
		if (!sym[i].removed) {
			nsymout++;
			if ((sym[i].info >> 4) == STB_LOCAL && sym[i].new_index > maxlocal)
				maxlocal = sym[i].new_index;
		}
	for (uint32_t k = 1; k < ns; k++) {
		objcopy_section *s = &sec[k];
		if (s->removed)
			continue;
		if (k == symtab_i) {
			s->size = (uint64_t)nsymout * sizeof(elf_symbol);
			s->generated_contents = objcopy_zeroed_alloc(s->size, 1);
			elf_symbol *d = (elf_symbol *)s->generated_contents;
			for (uint32_t i = 0; i < nsym; i++) {
				objcopy_symbol *y = &sym[i];
				if (y->removed)
					continue;
				uint16_t shndx = y->original_section_index;
				if (shndx && shndx < 0xff00)
					shndx = shndx < ns ? sec[shndx].new_index : 0;
				d[y->new_index] = (elf_symbol){.st_name = sn->entries[y->string_index].length ? (uint32_t)sn->entries[y->string_index].offset : 0, .st_info = y->info,
						  .st_other = y->other, .st_shndx = shndx, .st_value = y->value,
						  .st_size = y->size};
			}
			s->link = sec[s->link].new_index;
			s->info = maxlocal + 1;
		} else if (s->kind == OBJCOPY_SECTION_KIND_STRING_TABLE) {
			objcopy_string_table *t = k == shstr_i ? &names : (symtab_i != OBJCOPY_NONE && k == symstr_i) ? sn : NULL;
			if (!t) {
				/* an unreferenced string table: llvm-objcopy empties it */
				s->size = 1;
				s->generated_contents = objcopy_zeroed_alloc(1, 1);
				continue;
			}
			s->size = t->size;
			s->generated_contents = objcopy_zeroed_alloc(t->size, 1);
			objcopy_string_table_write(t, s->generated_contents);
		} else if (s->kind == OBJCOPY_SECTION_KIND_RELA) {
			s->generated_contents = objcopy_zeroed_alloc(s->size, 1);
			const elf_rela *r = (const elf_rela *)s->data;
			elf_rela *d = (elf_rela *)s->generated_contents;
			for (uint64_t j = 0; j < s->size / sizeof(elf_rela); j++) {
				uint32_t si = r[j].r_info >> 32;
				d[j] = r[j];
				d[j].r_info = (uint64_t)(si ? sym[si].new_index : 0) << 32 | (uint32_t)r[j].r_info;
			}
			s->link = symtab_i != OBJCOPY_NONE ? sec[symtab_i].new_index : 0;
			s->info = s->info ? sec[s->info].new_index : 0;
		} else if (s->link) {
			s->link = s->link < ns && !sec[s->link].removed ? sec[s->link].new_index : 0;
		}
	}

	/* layout: segments, then sections outside segments by original offset */
	VAUTOFREE uint32_t *order = objcopy_zeroed_alloc(ng, 4);
	for (uint32_t i = 0; i < ng; i++)
		order[i] = i;
	for (uint32_t i = 1; i < ng; i++) {
		uint32_t x = order[i], j = i;
		while (j > 0 && objcopy_segment_is_before(&seg[x], &seg[order[j - 1]])) {
			order[j] = order[j - 1];
			j--;
		}
		order[j] = x;
	}
	uint64_t off = 0;
	for (uint32_t i = 0; i < ng; i++) {
		objcopy_segment *g = &seg[order[i]];
		if (g->parent_segment >= 0)
			g->offset = seg[g->parent_segment].offset + g->original_offset - seg[g->parent_segment].original_offset;
		else
			g->offset = objcopy_value_align_skew(off, g->alignment ? g->alignment : 1, g->virtual_address);
		if (g->offset + g->file_size > off)
			off = g->offset + g->file_size;
	}
	VAUTOFREE uint32_t *outside = objcopy_zeroed_alloc(ns, 4);
	uint32_t nout = 0;
	for (uint32_t k = 1; k < ns; k++) {
		objcopy_section *s = &sec[k];
		if (s->removed)
			continue;
		if (s->parent_segment >= 0)
			s->new_offset = seg[s->parent_segment].offset + s->original_offset - seg[s->parent_segment].original_offset;
		else
			outside[nout++] = k;
	}
	for (uint32_t i = 1; i < nout; i++) {
		uint32_t x = outside[i], j = i;
		while (j > 0 && sec[outside[j - 1]].original_offset > sec[x].original_offset) {
			outside[j] = outside[j - 1];
			j--;
		}
		outside[j] = x;
	}
	for (uint32_t i = 0; i < nout; i++) {
		objcopy_section *s = &sec[outside[i]];
		if (!(objcopy_options.keep_debug && s->type == SHT_NOBITS))	/* (llvm: not aligned there) */
			off = objcopy_value_align_up(off, s->alignment ? s->alignment : 1);
		s->new_offset = off;
		if (s->type != SHT_NOBITS)
			off += s->size;
	}
	uint64_t shoff = objcopy_value_align_up(off, 8);
	uint64_t total = shoff + (uint64_t)(nkept + 1) * sizeof(elf_section_header);
	uint8_t *b = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

	/* segment bytes, then zero removed sections inside segments */
	for (uint32_t i = 0; i < e.program_header_count; i++)
		memcpy(b + seg[i].offset, e.file_bytes + seg[i].original_offset, seg[i].file_size);
	for (uint32_t k = 1; k < ns; k++) {
		objcopy_section *s = &sec[k];
		if (!s->removed || s->parent_segment < 0 || s->type == SHT_NOBITS || !s->size)
			continue;
		memset(b + s->original_offset - seg[s->parent_segment].original_offset + seg[s->parent_segment].offset, 0, s->size);
	}
	elf_file_header *oh = (elf_file_header *)b;
	memset(oh->e_ident, 0, 16);
	memcpy(oh->e_ident, "\177ELF\2\1\1", 7);
	oh->e_ident[7] = eh->e_ident[7];
	oh->e_ident[8] = eh->e_ident[8];
	oh->e_type = eh->e_type;
	oh->e_machine = eh->e_machine;
	oh->e_version = eh->e_version;
	oh->e_entry = eh->e_entry;
	oh->e_phnum = e.program_header_count;
	oh->e_phoff = e.program_header_count ? seg[phs].offset : 0;
	oh->e_phentsize = e.program_header_count ? sizeof(elf_program_header) : 0;
	oh->e_flags = eh->e_flags;
	oh->e_ehsize = sizeof(elf_file_header);
	oh->e_shentsize = sizeof(elf_section_header);
	oh->e_shoff = shoff;
	oh->e_shnum = nkept + 1;
	oh->e_shstrndx = sec[shstr_i].new_index;
	for (uint32_t i = 0; i < e.program_header_count; i++) {
		elf_program_header *p = (elf_program_header *)(b + seg[phs].offset) + i;
		*p = seg[i].program_header;
		p->p_offset = seg[i].offset;
	}
	elf_section_header *sh = (elf_section_header *)(b + shoff);
	/* llvm-objcopy: .llvm_addrsig's sh_link is dropped once sections or symbols may be removed */
	bool changed = objcopy_options.strip_all || objcopy_options.strip_debug || objcopy_options.remove_count || objcopy_options.only_count || objcopy_options.unneeded_count;
	for (uint32_t k = 1; k < ns; k++) {
		objcopy_section *s = &sec[k];
		if (s->removed)
			continue;
		if (s->type != SHT_NOBITS && s->size)
			memcpy(b + s->new_offset, s->generated_contents ? s->generated_contents : s->data, s->size);
		sh[s->new_index] = (elf_section_header){.sh_name = (uint32_t)objcopy_string_table_find(&names, objcopy_section_name(s)), .sh_type = s->type,
				    .sh_flags = s->flags, .sh_addr = s->address, .sh_offset = s->new_offset, .sh_size = s->size,
				    .sh_link = s->type == 0x6fff4c03 && changed ? 0 : s->link, .sh_info = s->info, .sh_addralign = s->alignment,
				    .sh_entsize = s->entry_size};
	}
	int rc = objcopy_error_count ? 1 : objcopy_file_write(dirfd, objcopy_options.output_path, b, total, rel ? 0666 : 0777);
	munmap(b, total);
	return rc;
}

static void objcopy_name_list_append(const char ***v, uint32_t *n, const char *s)
{
	*v = realloc(*v, (*n + 1) * sizeof(char *));
	(*v)[(*n)++] = s;
}

int objcopy_main(int argc, char **argv, int dirfd, FILE *err)
{
	objcopy_error_stream = err ? err : stderr;
	objcopy_error_count = 0;
	objcopy_program_name = "vobjcopy";
	memset(&objcopy_options, 0, sizeof objcopy_options);
	const char *files[2];
	int nf = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define OBJCOPY_ARGUMENT_IS(s) (!strcmp(a, s))
#define OBJCOPY_ARGUMENT_VALUE(s) (!strncmp(a, s "=", sizeof(s)) ? a + sizeof(s) : (OBJCOPY_ARGUMENT_IS(s) && i + 1 < argc) ? argv[++i] : NULL)
		const char *v;
		if ((v = OBJCOPY_ARGUMENT_VALUE("-O")) || (v = OBJCOPY_ARGUMENT_VALUE("--output-target"))) {
			/* binary, or this ELF64 little-endian aarch64 format unchanged */
			objcopy_options.binary = !strcmp(v, "binary");
			if (!objcopy_options.binary && strcmp(v, "elf64-littleaarch64") && strcmp(v, "elf64-aarch64"))
				objcopy_error_report("invalid output format: '%s'", v);
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("-I")) || (v = OBJCOPY_ARGUMENT_VALUE("--input-target"))) {
			objcopy_options.binary_input = !strcmp(v, "binary");
			if (!objcopy_options.binary_input && strcmp(v, "elf64-littleaarch64") && strcmp(v, "elf64-aarch64"))
				objcopy_error_report("unsupported input format: '%s'", v);
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("-B")) || (v = OBJCOPY_ARGUMENT_VALUE("--binary-architecture"))) {
			/* (only meaningful with -I binary: aarch64 is the one target) */
		} else if (OBJCOPY_ARGUMENT_IS("-S") || OBJCOPY_ARGUMENT_IS("--strip-all")) {
			objcopy_options.strip_all = true;
		} else if (OBJCOPY_ARGUMENT_IS("--only-keep-debug")) {
			objcopy_options.keep_debug = true;
		} else if (OBJCOPY_ARGUMENT_IS("-g") || OBJCOPY_ARGUMENT_IS("--strip-debug")) {
			objcopy_options.strip_debug = true;
		} else if (OBJCOPY_ARGUMENT_IS("-w") || OBJCOPY_ARGUMENT_IS("--wildcard")) {
			objcopy_options.wildcard = true;
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("--remove-section")) || (v = OBJCOPY_ARGUMENT_VALUE("-R"))) {
			objcopy_name_list_append(&objcopy_options.remove_names, &objcopy_options.remove_count, v);
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("--only-section")) || (v = OBJCOPY_ARGUMENT_VALUE("-j"))) {
			objcopy_name_list_append(&objcopy_options.only_sections, &objcopy_options.only_count, v);
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("--strip-unneeded-symbol"))) {
			objcopy_name_list_append(&objcopy_options.unneeded_symbols, &objcopy_options.unneeded_count, v);
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("--dump-section"))) {
			objcopy_name_list_append(&objcopy_options.dump_sections, &objcopy_options.dump_count, v);
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("--set-section-flags"))) {
			if (!strchr(v, '=') || v[0] == '=')
				objcopy_error_report("bad format for --set-section-flags: missing '='");
			objcopy_name_list_append(&objcopy_options.set_section_flags, &objcopy_options.set_section_flags_count, v);
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("--rename-section"))) {
			if (!strchr(v, '=') || v[0] == '=')
				objcopy_error_report("bad format for --rename-section: missing '='");
			objcopy_name_list_append(&objcopy_options.rename_sections, &objcopy_options.rename_sections_count, v);
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("--add-gnu-debuglink"))) {
			objcopy_options.debuglink = v;
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("--add-section"))) {
			if (!strchr(v, '=') || v[0] == '=')
				objcopy_error_report("bad format for --add-section: missing '='");
			objcopy_name_list_append(&objcopy_options.add_sections, &objcopy_options.add_count, v);
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("--redefine-sym"))) {
			if (!strchr(v, '='))
				objcopy_error_report("bad format for --redefine-sym");
			objcopy_name_list_append(&objcopy_options.redefine_symbols, &objcopy_options.redefine_count, v);
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("--prefix-symbols"))) {
			objcopy_options.prefix_symbols = v;
		} else if ((v = OBJCOPY_ARGUMENT_VALUE("--prefix-alloc-sections"))) {
			objcopy_options.prefix_alloc_sections = v;
		} else if (a[0] == '-' && a[1]) {
			objcopy_error_report("unsupported option %s", a);
		} else if (nf < 2) {
			files[nf++] = a;
		} else {
			objcopy_error_report("too many file arguments");
		}
#undef OBJCOPY_ARGUMENT_IS
#undef OBJCOPY_ARGUMENT_VALUE
	}
	/* llvm-objcopy: flags of a renamed section (by either name) are --rename-section's */
	for (uint32_t i = 0; i < objcopy_options.set_section_flags_count; i++)
		for (uint32_t j = 0; j < objcopy_options.rename_sections_count; j++) {
			const char *eq = strchr(objcopy_options.rename_sections[j], '='), *nw = eq + 1;
			size_t sl = strcspn(objcopy_options.set_section_flags[i], "="), ol = (size_t)(eq - objcopy_options.rename_sections[j]), nl = strcspn(nw, ",");
			if ((sl == ol && !strncmp(objcopy_options.set_section_flags[i], objcopy_options.rename_sections[j], sl)) || (sl == nl && !strncmp(objcopy_options.set_section_flags[i], nw, sl)))
				objcopy_error_report("--set-section-flags=%.*s conflicts with --rename-section=%.*s=%.*s",
				       (int)sl, objcopy_options.set_section_flags[i], (int)ol, objcopy_options.rename_sections[j], (int)nl, nw);
		}
	if (nf < 1)
		objcopy_error_report("no input file");
	int rc = 1;
	if (!objcopy_error_count) {
		objcopy_options.input_path = files[0];
		objcopy_options.output_path = nf > 1 ? files[1] : files[0];
		rc = objcopy_options.binary_input ? objcopy_binary_input(dirfd) : objcopy_run(dirfd);
	}
	free(objcopy_options.remove_names);
	free(objcopy_options.only_sections);
	free(objcopy_options.unneeded_symbols);
	free(objcopy_options.dump_sections);
	free(objcopy_options.redefine_symbols);
	free(objcopy_options.add_sections);
	free(objcopy_options.set_section_flags);
	free(objcopy_options.rename_sections);
	return rc || objcopy_error_count;
}

/* ================= nm ================= */

typedef struct {
	const char *name;
	uint64_t addr, size;
	char type;
	bool defined;
} nm_symbol;

static int nm_symbol_name_cmp(const void *a, const void *b)
{
	const nm_symbol *x = a, *y = b;
	int c = strcmp(x->name, y->name);
	if (c)
		return c;
	if (x->size != y->size)
		return x->size < y->size ? -1 : 1;
	return (x->addr > y->addr) - (x->addr < y->addr);
}
static int nm_symbol_address_cmp(const void *a, const void *b)
{
	const nm_symbol *x = a, *y = b;
	if (x->defined != y->defined)
		return x->defined ? 1 : -1;
	if (x->addr != y->addr)
		return x->addr < y->addr ? -1 : 1;
	int c = strcmp(x->name, y->name);
	if (c)
		return c;
	return (x->size > y->size) - (x->size < y->size);
}

static char nm_symbol_type_letter(const objcopy_elf *e, const elf_symbol *y)
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
	} else if (y->st_shndx < e->section_header_count) {
		const elf_section_header *s = &e->section_headers[y->st_shndx];
		if (s->sh_flags & SHF_EXECINSTR)
			c = 't';
		else if (s->sh_type == SHT_NOBITS)
			c = 'b';
		else if (s->sh_flags & SHF_ALLOC)
			c = s->sh_flags & SHF_WRITE ? 'd' : 'r';
		else if (!strncmp(e->section_name_table + s->sh_name, ".debug", 6))
			c = 'N';
		else if (!(s->sh_flags & SHF_WRITE))
			c = 'n';
	}
	if (global && c >= 'a' && c <= 'z')
		c -= 32;
	return c;
}

/* nm's inputs: files, or the members of an archive (regular: copied out, 8-aligned; thin: by path) */
typedef struct { char *label, *path; uint8_t *member_bytes; size_t size; bool is_member; const char *archive; } nm_input;

static void nm_input_append(nm_input **v, int *n, nm_input x)
{
	*v = realloc(*v, (size_t)(*n + 1) * sizeof **v);
	(*v)[(*n)++] = x;
}

static void nm_inputs_collect(int dirfd, const char *path, nm_input **v, int *n)
{
	VAUTOCLOSE int fd = openat(dirfd, path, O_RDONLY);
	struct stat st;
	char magic[8];

	if (fd < 0 || fstat(fd, &st) < 0 || st.st_size < 8 || pread(fd, magic, 8, 0) != 8 ||
	    varchive_format_detect(magic, 8) == VARCHIVE_FORMAT_NONE) {
		nm_input_append(v, n, (nm_input){.label = strdup(path), .path = strdup(path)});
		return;
	}
	VAUTOUNMAP vfile_mapping m;
	if (!vfile_map_fd(fd, &m, &st)) {
		objcopy_error_report("'%s': %s", path, strerror(errno));
		return;
	}
	const char *slash = strrchr(path, '/');
	varchive_cursor cursor;
	varchive_member member;

	VCLOSE(fd);	/* (the mapping outlives the descriptor) */
	varchive_cursor_init(&cursor, m.p, m.n);
	for (; varchive_member_next(&cursor, &member); varchive_cursor_advance(&cursor, &member)) {
		char name[4096];

		if (!member.header_valid || (member.has_data && !member.data_fits)) {
			objcopy_error_report("'%s': truncated or malformed archive", path);
			break;
		}
		if (member.kind != VARCHIVE_MEMBER_FILE)
			continue;
		/* a long name not in "//" is empty */
		snprintf(name, sizeof name, "%.*s", member.name ? (int)member.name_length : 0, member.name ? member.name : "");
		nm_input x = {.label = strdup(name), .is_member = true, .archive = path};
		if (cursor.thin) {
			char full[8192];

			if (name[0] == '/' || !slash)
				snprintf(full, sizeof full, "%s", name);
			else
				snprintf(full, sizeof full, "%.*s/%s", (int)(slash - path), path, name);
			x.path = strdup(full);
		} else {
			x.member_bytes = objcopy_zeroed_alloc(member.size + 8, 1);
			memcpy(x.member_bytes, member.data, member.size);
			x.size = member.size;
		}
		nm_input_append(v, n, x);
	}
}

int nm_main(int argc, char **argv, int dirfd, FILE *out, FILE *err)
{
	objcopy_error_stream = err ? err : stderr;
	objcopy_error_count = 0;
	objcopy_program_name = "vnm";
	bool nosort = false, numeric = false, size = false, undef = false, defd = false, ext = false, dyn = false;
	bool fname = false;	/* -A/-o: each line starts with the file (archive:member) */
	const char **files = objcopy_zeroed_alloc((size_t)argc + 1, sizeof *files);
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
				else if (*p == 'D') dyn = true;
				else if (*p == 'A' || *p == 'o') fname = true;
				else objcopy_error_report("unsupported option -%c", *p);
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
		} else if (!strcmp(a, "--dynamic")) {
			dyn = true;
		} else if (!strcmp(a, "--print-file-name")) {
			fname = true;
		} else if (!strcmp(a, "--extern-only")) {
			ext = true;
		} else if (a[0] == '-' && a[1]) {
			objcopy_error_report("unknown argument '%s'", a);
		} else {
			files[nf++] = a;
		}
	}
	if (!nf)
		files[nf++] = "a.out";
	if (objcopy_error_count) {
		free(files);
		return 1;
	}
	nm_input *in = NULL;
	int nin = 0;
	for (int f = 0; f < nf; f++)
		nm_inputs_collect(dirfd, files[f], &in, &nin);
	for (int f = 0; f < nin; f++) {
		objcopy_elf e;
		if (in[f].member_bytes) {
			memset(&e, 0, sizeof e);
			e.file_bytes = in[f].member_bytes;
			e.size = in[f].size;
			e.file_header = (const elf_file_header *)e.file_bytes;
			if (e.size < sizeof(elf_file_header) || memcmp(e.file_bytes, "\177ELF\2\1", 6) || !objcopy_elf_is_valid(&e)) {
				objcopy_error_report("'%s': not a valid ELF64 little-endian file", in[f].label);
				continue;
			}
		} else if (objcopy_elf_open(&e, dirfd, in[f].path))
			continue;
		const elf_section_header *st = NULL;
		for (uint32_t k = 0; k < e.section_header_count; k++)
			if (e.section_headers[k].sh_type == (dyn ? SHT_DYNSYM : SHT_SYMTAB))
				st = &e.section_headers[k];
		if ((nf > 1 || in[f].is_member) && !fname)
			fprintf(out, "\n%s:\n", in[f].label);
		if (!st) {
			fprintf(objcopy_error_stream, "%s: %s: no symbols\n", objcopy_program_name, in[f].label);
			if (!in[f].member_bytes)
				objcopy_elf_close(&e);
			continue;
		}
		const elf_symbol *ys = (const elf_symbol *)(e.file_bytes + st->sh_offset);
		const char *str = (const char *)e.file_bytes + e.section_headers[st->sh_link].sh_offset;
		uint32_t n = st->sh_size / sizeof(elf_symbol);
		nm_symbol *v = objcopy_zeroed_alloc(n, sizeof *v);
		uint32_t m = 0;
		/* -D: symbol versions as llvm-nm (name@@VER defined, name@VER hidden or undefined) */
		const uint16_t *versym = NULL;
		const char *vname[256] = { 0 };
		char **owned = dyn ? objcopy_zeroed_alloc(n, sizeof *owned) : NULL;
		for (uint32_t k = 0; dyn && k < e.section_header_count; k++) {
			const elf_section_header *h = &e.section_headers[k];
			const uint8_t *d = e.file_bytes + h->sh_offset;
			const char *vs = h->sh_link < e.section_header_count ? (const char *)e.file_bytes + e.section_headers[h->sh_link].sh_offset : NULL;
			if (h->sh_type == 0x6fffffff && h->sh_size >= n * 2)
				versym = (const uint16_t *)d;
			else if (h->sh_type == 0x6ffffffd)	/* verdef */
				for (uint32_t c = 0, o = 0; c < h->sh_info && o + 20 <= h->sh_size; c++) {
					uint16_t ndx, cnt; uint32_t aux, next, nm_;
					memcpy(&ndx, d + o + 4, 2); memcpy(&cnt, d + o + 6, 2);
					memcpy(&aux, d + o + 12, 4); memcpy(&next, d + o + 16, 4);
					if (cnt && o + aux + 8 <= h->sh_size && ndx < 256) {
						memcpy(&nm_, d + o + aux, 4);
						vname[ndx] = vs + nm_;
					}
					if (!next)
						break;
					o += next;
				}
			else if (h->sh_type == 0x6ffffffe)	/* verneed */
				for (uint32_t c = 0, o = 0; c < h->sh_info && o + 16 <= h->sh_size; c++) {
					uint16_t cnt; uint32_t aux, next;
					memcpy(&cnt, d + o + 2, 2); memcpy(&aux, d + o + 8, 4); memcpy(&next, d + o + 12, 4);
					for (uint32_t j = 0, a = o + aux; j < cnt && a + 16 <= h->sh_size; j++) {
						uint16_t other; uint32_t nm_, an;
						memcpy(&other, d + a + 6, 2); memcpy(&nm_, d + a + 8, 4); memcpy(&an, d + a + 12, 4);
						if (other < 256)
							vname[other] = vs + nm_;
						if (!an)
							break;
						a += an;
					}
					if (!next)
						break;
					o += next;
				}
		}
		for (uint32_t i = 1; i < n; i++) {
			const elf_symbol *y = &ys[i];
			uint8_t type = y->st_info & 15;
			const char *name = str + y->st_name;
			if (type == STT_FILE || type == STT_SECTION)
				continue;
			if (e.file_header->e_machine == 183 && name[0] == '$' && (name[1] == 'd' || name[1] == 'x'))
				continue;
			if ((undef && y->st_shndx != SHN_UNDEF) || (defd && y->st_shndx == SHN_UNDEF) ||
			    (ext && (y->st_info >> 4) == STB_LOCAL))
				continue;
			if (versym && (versym[i] & 0x7fff) > 1 && (versym[i] & 0x7fff) < 256 && vname[versym[i] & 0x7fff]) {
				const char *vn = vname[versym[i] & 0x7fff];
				bool at2 = !(versym[i] & 0x8000) && y->st_shndx != SHN_UNDEF;
				size_t l = strlen(name) + strlen(vn) + 3;
				owned[i] = malloc(l);
				snprintf(owned[i], l, "%s%s%s", name, at2 ? "@@" : "@", vn);
				name = owned[i];
			}
			v[m++] = (nm_symbol){.name = name, .addr = y->st_value, .size = y->st_size,
					.type = nm_symbol_type_letter(&e, y), .defined = y->st_shndx != SHN_UNDEF};
		}
		if (!nosort)
			qsort(v, m, sizeof *v, numeric ? nm_symbol_address_cmp : nm_symbol_name_cmp);
		/* one buffered pass */
		char pfx[8192] = "";
		if (fname && in[f].is_member)
			snprintf(pfx, sizeof pfx, "%s:%s: ", in[f].archive, in[f].label);
		else if (fname)
			snprintf(pfx, sizeof pfx, "%s: ", in[f].label);
		size_t pl = strlen(pfx), cap = (size_t)m * (64 + pl) + 4096, len = 0;
		for (uint32_t i = 0; i < m; i++)
			cap += strlen(v[i].name);
		char *buf = malloc(cap);
		for (uint32_t i = 0; i < m; i++) {
			nm_symbol *s = &v[i];
			memcpy(buf + len, pfx, pl);
			len += pl;
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
		for (uint32_t i = 0; owned && i < n; i++)
			free(owned[i]);
		free(owned);
		free(v);
		if (!in[f].member_bytes)
			objcopy_elf_close(&e);
	}
	for (int f = 0; f < nin; f++) {
		free(in[f].label);
		free(in[f].path);
		free(in[f].member_bytes);
	}
	free(in);
	free(files);
	return objcopy_error_count ? 1 : 0;
}
