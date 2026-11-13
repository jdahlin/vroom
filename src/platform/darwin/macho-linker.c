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
#include "ld/ld.h"
#include "common/digest.h"

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

#define MACHO_NONE 0xffffffffu
#define MACHO_IMAGE_BASE 0x100000000ull
#define MACHO_PAGE_SIZE 0x4000ull

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
	char section_name[16], segment_name[16];
	uint64_t address, size;
	uint32_t offset, align, relocations_offset, relocation_count, flags, reserved1, reserved2, reserved3;
} macho_section_header;
typedef struct { uint32_t string_index; uint8_t type, section_number; uint16_t description; uint64_t value; } macho_nlist;
typedef struct { int32_t address; uint32_t info; } macho_relocation;

typedef struct {
	const char *path;
	uint8_t *file_bytes;
	size_t size;
	macho_section_header *sections;
	uint32_t section_count;
	uint32_t *output_sections;			/* per section: output section or MACHO_NONE (dropped) */
	uint64_t *section_offsets;			/* per section: offset in its output section */
	const macho_nlist *symbols;
	uint32_t symbol_count;
	const char *string_table;
	uint32_t string_table_size;
	uint32_t *global_ids;			/* per symbol: global id (external ones) or MACHO_NONE */
	uint32_t minimum_os_version;
} macho_object;

enum { SEG_TEXT, SEG_DATA_CONST, SEG_DATA, SEG_LINKEDIT, MACHO_SEGMENT_COUNT };
typedef struct {
	char segment_name[16], name[16];
	int segment_index;
	uint32_t flags, align;		/* align: bytes */
	uint64_t size, address, file_offset;
	bool zerofill;
	uint8_t *data;			/* output image pointer once laid out */
} macho_output_section;

typedef struct {
	const char *name;
	uint32_t object, symbol;		/* definition, or MACHO_NONE */
	bool weak, common;
	uint64_t common_size;			/* common: size */
	uint32_t common_alignment;
	uint32_t import_index;		/* index in imports, or MACHO_NONE */
	uint32_t got_slot, stub_slot;		/* slots, or MACHO_NONE */
	uint32_t first_reference_object;		/* first referencing object */
	uint64_t address;			/* final address of a definition */
} macho_global_symbol;
typedef struct { uint64_t address; uint64_t value; } macho_fixup;	/* val: bind import + 1 << 63, or rebase target */

/*
 * One Mach-O link's state, created by macho_link and freed by
 * macho_context_destroy.  The libSystem export table below is process-wide
 * (read once, by macho_libsystem_load).
 */
struct macho_context {
	struct ld_diagnostics diagnostics;	/* at most 20 errors, without lld's announcement */
	macho_object *objects;
	uint32_t object_count;
	macho_output_section *output_sections;
	uint32_t output_section_count, output_section_capacity;
	uint32_t text_section, stubs_section, got_section;	/* output sections, or MACHO_NONE */
	macho_global_symbol *globals;
	uint32_t global_count, global_capacity;
	uint32_t *global_hash_slots;		/* open addressing: id + 1 */
	uint32_t global_hash_mask;
	macho_fixup *fixups;
	uint32_t fixup_count, fixup_capacity;
	uint32_t *imports;			/* global ids */
	uint32_t import_count, import_capacity;
	uint32_t got_count, stub_count;
	uint32_t *got_globals, *stub_globals;	/* slot -> global */
};

static void macho_error_report(struct macho_context *ctx, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void macho_error_report(struct macho_context *ctx, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ld_diagnostic_report(&ctx->diagnostics, "error", fmt, ap);
	va_end(ap);
}

static void *macho_alloc(struct macho_context *ctx, size_t n)
{
	void *p = calloc(1, n ? n : 1);
	if (!p) {
		fputs("vld: out of memory\n", ctx->diagnostics.error_stream);
		exit(1);
	}
	return p;
}
#define MACHO_VECTOR_GROW(v, n, cap) do { if ((n) >= (cap)) { (cap) = (cap) ? 2 * (cap) : 16; \
	(v) = realloc((v), (cap) * sizeof(*(v))); if (!(v)) { fputs("vld: out of memory\n", ctx->diagnostics.error_stream); exit(1); } } } while (0)

static inline uint32_t macho_u32_load(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline void macho_u32_store(void *p, uint32_t v) { memcpy(p, &v, 4); }
static inline void macho_u64_store(void *p, uint64_t v) { memcpy(p, &v, 8); }
static inline void macho_u32_store_big_endian(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static inline void macho_u64_store_big_endian(uint8_t *p, uint64_t v) { macho_u32_store_big_endian(p, v >> 32); macho_u32_store_big_endian(p + 4, (uint32_t)v); }
static inline uint64_t macho_value_align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

/* ---------------- inputs ---------------- */



/* output sections */

static uint32_t macho_output_section_get(struct macho_context *ctx, const char *seg, const char *name, uint32_t flags)
{
	for (uint32_t i = 0; i < ctx->output_section_count; i++)
		if (!strncmp(ctx->output_sections[i].segment_name, seg, 16) && !strncmp(ctx->output_sections[i].name, name, 16))
			return i;
	MACHO_VECTOR_GROW(ctx->output_sections, ctx->output_section_count, ctx->output_section_capacity);
	macho_output_section *o = &ctx->output_sections[ctx->output_section_count];
	memset(o, 0, sizeof *o);
	strncpy(o->segment_name, seg, 16);
	strncpy(o->name, name, 16);
	o->flags = flags;
	o->align = 1;
	uint32_t t = flags & 0xff;
	o->zerofill = t == S_ZEROFILL || t == S_GB_ZEROFILL;
	o->segment_index = !strncmp(seg, "__TEXT", 16) ? SEG_TEXT : !strncmp(seg, "__DATA_CONST", 16) ? SEG_DATA_CONST : SEG_DATA;
	return ctx->output_section_count++;
}

/* globals */

static uint32_t macho_name_hash(const char *s)
{
	uint32_t h = 2166136261u;
	for (; *s; s++)
		h = (h ^ (uint8_t)*s) * 16777619u;
	return h;
}
static uint32_t macho_global_find_or_add(struct macho_context *ctx, const char *name, bool add)
{
	uint32_t h = macho_name_hash(name) & ctx->global_hash_mask;
	for (;; h = (h + 1) & ctx->global_hash_mask) {
		uint32_t v = ctx->global_hash_slots[h];
		if (!v)
			break;
		if (!strcmp(ctx->globals[v - 1].name, name))
			return v - 1;
	}
	if (!add)
		return MACHO_NONE;
	if (2 * (ctx->global_count + 1) > ctx->global_hash_mask) {	/* grow and rehash */
		uint32_t nm = 2 * (ctx->global_hash_mask + 1) - 1, *nh = macho_alloc(ctx, (nm + 1) * sizeof(uint32_t));
		for (uint32_t i = 0; i < ctx->global_count; i++) {
			uint32_t k = macho_name_hash(ctx->globals[i].name) & nm;
			while (nh[k])
				k = (k + 1) & nm;
			nh[k] = i + 1;
		}
		free(ctx->global_hash_slots);
		ctx->global_hash_slots = nh;
		ctx->global_hash_mask = nm;
		return macho_global_find_or_add(ctx, name, add);
	}
	MACHO_VECTOR_GROW(ctx->globals, ctx->global_count, ctx->global_capacity);
	macho_global_symbol *g = &ctx->globals[ctx->global_count];
	memset(g, 0, sizeof *g);
	g->name = name;
	g->object = g->symbol = g->import_index = g->got_slot = g->stub_slot = g->first_reference_object = MACHO_NONE;
	ctx->global_hash_slots[h] = ctx->global_count + 1;
	return ctx->global_count++;
}

/* ---------------- libSystem exports (SDK .tbd) ---------------- */

static const char *macho_sdk_root_find(void)
{
	const char *s = getenv("SDKROOT");
	return s && *s ? s : "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";
}

static char *macho_libsystem_text;			/* libSystem.B.tbd, NUL-terminated */
static uint32_t *macho_export_hash_slots, macho_export_hash_mask;	/* exported names: offsets into tbd + 1 */
static uint32_t macho_sdk_version;		/* SDKSettings "Version", xxxx.yy.zz */

static char *macho_file_read(struct macho_context *ctx, const char *path, size_t *n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return NULL;
	struct stat st;
	if (fstat(fd, &st)) {
		close(fd);
		return NULL;
	}
	char *b = macho_alloc(ctx, (size_t)st.st_size + 1);
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

static uint32_t macho_version_parse(const char *s)
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
static bool macho_libsystem_read(struct macho_context *ctx)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/usr/lib/libSystem.B.tbd", macho_sdk_root_find());
	size_t n;
	if (!(macho_libsystem_text = macho_file_read(ctx, path, &n))) {
		macho_error_report(ctx, "cannot read %s: %s", path, strerror(errno));
		return false;
	}
	snprintf(path, sizeof path, "%s/SDKSettings.json", macho_sdk_root_find());
	char *js = macho_file_read(ctx, path, NULL);
	const char *v = js ? strstr(js, "\"Version\":\"") : NULL;
	macho_sdk_version = v ? macho_version_parse(v + 11) : 0;
	free(js);
	macho_export_hash_mask = (1u << 15) - 1;
	macho_export_hash_slots = macho_alloc(ctx, (macho_export_hash_mask + 1) * sizeof(uint32_t));
	uint32_t count = 0;
	bool arm = false;
	char *p = macho_libsystem_text, *end = macho_libsystem_text + n;
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
				if (2 * (count + 1) > macho_export_hash_mask) {
					uint32_t nm = 2 * (macho_export_hash_mask + 1) - 1, *nh = macho_alloc(ctx, (nm + 1) * sizeof(uint32_t));
					for (uint32_t i = 0; i <= macho_export_hash_mask; i++)
						if (macho_export_hash_slots[i]) {
							uint32_t h = macho_name_hash(macho_libsystem_text + macho_export_hash_slots[i] - 1) & nm;
							while (nh[h])
								h = (h + 1) & nm;
							nh[h] = macho_export_hash_slots[i];
						}
					free(macho_export_hash_slots);
					macho_export_hash_slots = nh;
					macho_export_hash_mask = nm;
				}
				uint32_t h = macho_name_hash(b) & macho_export_hash_mask;
				while (macho_export_hash_slots[h] && strcmp(macho_libsystem_text + macho_export_hash_slots[h] - 1, b))
					h = (h + 1) & macho_export_hash_mask;
				if (!macho_export_hash_slots[h]) {
					macho_export_hash_slots[h] = (uint32_t)(b - macho_libsystem_text) + 1;
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

/* the export table, read by the first link that needs it; links run concurrently, so the
   first one reads it under a lock and the others wait for it */
static bool macho_libsystem_load(struct macho_context *ctx)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_lock(&lock);
	bool ok = macho_libsystem_text || macho_libsystem_read(ctx);
	pthread_mutex_unlock(&lock);
	return ok;
}

static bool macho_libsystem_is_exported(const char *name)
{
	uint32_t h = macho_name_hash(name) & macho_export_hash_mask;
	for (; macho_export_hash_slots[h]; h = (h + 1) & macho_export_hash_mask)
		if (!strcmp(macho_libsystem_text + macho_export_hash_slots[h] - 1, name))
			return true;
	return false;
}

/* ---------------- reading objects ---------------- */

static bool macho_object_read(struct macho_context *ctx, macho_object *o)
{
	const uint8_t *b = o->file_bytes;
	if (o->size < 32 || macho_u32_load(b) != MH_MAGIC_64 || macho_u32_load(b + 4) != CPU_ARM64 || macho_u32_load(b + 12) != MH_OBJECT) {
		macho_error_report(ctx, "%s: not an arm64 Mach-O object", o->path);
		return false;
	}
	uint32_t ncmds = macho_u32_load(b + 16), sizeofcmds = macho_u32_load(b + 20);
	if (32 + (uint64_t)sizeofcmds > o->size) {
		macho_error_report(ctx, "%s: truncated load commands", o->path);
		return false;
	}
	const uint8_t *lc = b + 32;
	for (uint32_t i = 0; i < ncmds; i++) {
		uint32_t cmd = macho_u32_load(lc), sz = macho_u32_load(lc + 4);
		if (sz < 8 || lc + sz > b + 32 + sizeofcmds) {
			macho_error_report(ctx, "%s: bad load command", o->path);
			return false;
		}
		if (cmd == LC_SEGMENT_64) {
			uint32_t ns = macho_u32_load(lc + 64);
			if (72 + (uint64_t)ns * 80 > sz) {
				macho_error_report(ctx, "%s: bad segment", o->path);
				return false;
			}
			o->sections = macho_alloc(ctx, ns * sizeof(macho_section_header));
			memcpy(o->sections, lc + 72, ns * sizeof(macho_section_header));
			o->section_count = ns;
		} else if (cmd == LC_SYMTAB) {
			uint32_t symoff = macho_u32_load(lc + 8), nsyms = macho_u32_load(lc + 12), stroff = macho_u32_load(lc + 16), strsize = macho_u32_load(lc + 20);
			if ((uint64_t)symoff + (uint64_t)nsyms * 16 > o->size || (uint64_t)stroff + strsize > o->size) {
				macho_error_report(ctx, "%s: bad symbol table", o->path);
				return false;
			}
			o->symbols = (const macho_nlist *)(const void *)(b + symoff);
			o->symbol_count = nsyms;
			o->string_table = (const char *)b + stroff;
			o->string_table_size = strsize;
		} else if (cmd == LC_BUILD_VERSION) {
			o->minimum_os_version = macho_u32_load(lc + 12);
		}
		lc += sz;
	}
	for (uint32_t s = 0; s < o->section_count; s++) {
		macho_section_header *x = &o->sections[s];
		uint32_t t = x->flags & 0xff;
		bool zf = t == S_ZEROFILL || t == S_GB_ZEROFILL;
		if (!zf && (uint64_t)x->offset + x->size > o->size) {
			macho_error_report(ctx, "%s: section %.16s out of range", o->path, x->section_name);
			return false;
		}
		if ((uint64_t)x->relocations_offset + (uint64_t)x->relocation_count * 8 > o->size) {
			macho_error_report(ctx, "%s: relocations of %.16s out of range", o->path, x->section_name);
			return false;
		}
	}
	return true;
}

static const char *macho_object_symbol_name(const macho_object *o, uint32_t i)
{
	uint32_t x = o->symbols[i].string_index;
	return x < o->string_table_size ? o->string_table + x : "";
}

/* ---------------- the link ---------------- */


static void macho_fixup_add(struct macho_context *ctx, uint64_t addr, uint64_t val)
{
	MACHO_VECTOR_GROW(ctx->fixups, ctx->fixup_count, ctx->fixup_capacity);
	ctx->fixups[ctx->fixup_count++] = (macho_fixup){addr, val};
}

static uint32_t macho_import_index(struct macho_context *ctx, uint32_t g)
{
	if (ctx->globals[g].import_index == MACHO_NONE) {
		MACHO_VECTOR_GROW(ctx->imports, ctx->import_count, ctx->import_capacity);
		ctx->globals[g].import_index = ctx->import_count;
		ctx->imports[ctx->import_count++] = g;
	}
	return ctx->globals[g].import_index;
}
static uint32_t macho_got_slot_index(struct macho_context *ctx, uint32_t g)
{
	if (ctx->globals[g].got_slot == MACHO_NONE) {
		ctx->globals[g].got_slot = ctx->got_count++;
		ctx->got_globals = realloc(ctx->got_globals, ctx->got_count * sizeof(uint32_t));
		ctx->got_globals[ctx->got_count - 1] = g;
	}
	return ctx->globals[g].got_slot;
}
static uint32_t macho_stub_index(struct macho_context *ctx, uint32_t g)
{
	if (ctx->globals[g].stub_slot == MACHO_NONE) {
		macho_got_slot_index(ctx, g);
		ctx->globals[g].stub_slot = ctx->stub_count++;
		ctx->stub_globals = realloc(ctx->stub_globals, ctx->stub_count * sizeof(uint32_t));
		ctx->stub_globals[ctx->stub_count - 1] = g;
	}
	return ctx->globals[g].stub_slot;
}

/* address of symbol i of object o (defined), or of the global it resolves to */
typedef struct { bool is_import; uint32_t global; uint64_t address; } macho_target;

static uint64_t macho_local_symbol_address(struct macho_context *ctx, const macho_object *o, uint32_t i)
{
	const macho_nlist *n = &o->symbols[i];
	if ((n->type & N_TYPE) == N_ABS)
		return n->value;
	uint32_t s = n->section_number - 1u;
	if (s >= o->section_count || o->output_sections[s] == MACHO_NONE)
		return 0;
	return ctx->output_sections[o->output_sections[s]].address + o->section_offsets[s] + (n->value - o->sections[s].address);
}

static bool macho_relocation_target_find(struct macho_context *ctx, const macho_object *o, uint32_t symi, macho_target *t)
{
	if (symi >= o->symbol_count) {
		macho_error_report(ctx, "%s: bad relocation symbol", o->path);
		return false;
	}
	t->is_import = false;
	t->global = o->global_ids[symi];
	if (t->global == MACHO_NONE) {
		t->address = macho_local_symbol_address(ctx, o, symi);
		return true;
	}
	macho_global_symbol *g = &ctx->globals[t->global];
	if (g->import_index != MACHO_NONE) {
		t->is_import = true;
		return true;
	}
	t->address = g->address;
	return true;
}

static int macho_load_store_scale(uint32_t ins)
{
	/* load/store (unsigned immediate): size in 31:30; 128-bit vector when opc<1> and size 00 */
	if ((ins & 0x3b000000) != 0x39000000)
		return 0;
	int sz = (int)(ins >> 30);
	if (sz == 0 && (ins & 0x04800000) == 0x04800000)
		return 4;
	return sz;
}

static void macho_page21_store(struct macho_context *ctx, uint8_t *loc, uint64_t S, uint64_t P, const char *path)
{
	int64_t d = (int64_t)((S & ~0xfffull) - (P & ~0xfffull)) >> 12;
	if (d < -(1 << 20) || d >= (1 << 20))
		macho_error_report(ctx, "%s: ADRP out of range", path);
	uint32_t ins = macho_u32_load(loc) & ~(0x60000000u | 0x00ffffe0u);
	macho_u32_store(loc, ins | (uint32_t)(d & 3) << 29 | (uint32_t)((d >> 2) & 0x7ffff) << 5);
}
static void macho_lo12_store(uint8_t *loc, uint64_t S)
{
	uint32_t ins = macho_u32_load(loc);
	int sh = macho_load_store_scale(ins);
	uint32_t imm = (uint32_t)(S & 0xfff) >> sh;
	macho_u32_store(loc, (ins & ~(0xfffu << 10)) | imm << 10);
}

/* pass over all relocations: kind 0 scans (stubs, GOT slots, fixups), 1 applies */
static void macho_relocations_process(struct macho_context *ctx, int pass, uint8_t *img)
{
	for (uint32_t oi = 0; oi < ctx->object_count; oi++) {
		macho_object *o = &ctx->objects[oi];
		for (uint32_t s = 0; s < o->section_count; s++) {
			macho_section_header *x = &o->sections[s];
			if (o->output_sections[s] == MACHO_NONE || !x->relocation_count)
				continue;
			macho_output_section *os = &ctx->output_sections[o->output_sections[s]];
			const macho_relocation *r = (const macho_relocation *)(const void *)(o->file_bytes + x->relocations_offset);
			uint64_t secaddr = os->address + o->section_offsets[s];
			uint8_t *sb = pass ? img + os->file_offset + o->section_offsets[s] : NULL;
			int64_t addend = 0;
			for (uint32_t k = 0; k < x->relocation_count; k++) {
				uint32_t a = (uint32_t)r[k].address, info = r[k].info;
				uint32_t symnum = info & 0xffffff, pcrel = (info >> 24) & 1, len = (info >> 25) & 3;
				uint32_t ext = (info >> 27) & 1, type = info >> 28;
				if ((int32_t)a < 0 || a + (1u << len) > x->size) {
					macho_error_report(ctx, "%s: relocation outside %.16s", o->path, x->section_name);
					continue;
				}
				if (type == R_ADDEND) {
					addend = (int32_t)(symnum << 8) >> 8;
					continue;
				}
				if (!ext) {
					macho_error_report(ctx, "%s: section-relative relocations are not supported", o->path);
					continue;
				}
				macho_target t;
				if (!macho_relocation_target_find(ctx, o, symnum, &t))
					continue;
				uint64_t P = secaddr + a;
				uint8_t *loc = sb ? sb + a : NULL;
				int64_t A = addend;
				addend = 0;
				switch (type) {
				case R_UNSIGNED: {
					if (len != 3 || pcrel) {
						macho_error_report(ctx, "%s: UNSIGNED relocation of %u bytes is not supported", o->path, 1u << len);
						break;
					}
					int64_t inplace = 0;
					memcpy(&inplace, o->file_bytes + x->offset + a, 8);
					if (!pass) {
						if (t.is_import)
							macho_fixup_add(ctx, P, 1ull << 63 | (uint64_t)ctx->globals[t.global].import_index | (uint64_t)(uint8_t)inplace << 32);
						else
							macho_fixup_add(ctx, P, t.address + (uint64_t)inplace);
						if (t.is_import && (inplace < 0 || inplace > 255))
							macho_error_report(ctx, "%s: pointer to %s with addend %lld", o->path, ctx->globals[t.global].name, (long long)inplace);
					}
					/* the fixup chain is written over the slot later */
					break;
				}
				case R_BRANCH26: {
					if (!pass) {
						if (t.is_import)
							macho_stub_index(ctx, t.global);
						break;
					}
					uint64_t S = t.is_import ? ctx->output_sections[ctx->stubs_section].address + 12ull * ctx->globals[t.global].stub_slot : t.address;
					int64_t d = (int64_t)(S + (uint64_t)A - P);
					if ((d & 3) || d < -(1ll << 27) || d >= (1ll << 27))
						macho_error_report(ctx, "%s: branch out of range", o->path);
					macho_u32_store(loc, (macho_u32_load(loc) & 0xfc000000u) | ((uint32_t)(d >> 2) & 0x03ffffffu));
					break;
				}
				case R_PAGE21:
				case R_PAGEOFF12:
					if (t.is_import) {
						if (!pass)
							macho_error_report(ctx, "%s: direct reference to imported %s", o->path, ctx->globals[t.global].name);
						break;
					}
					if (!pass)
						break;
					if (type == R_PAGE21)
						macho_page21_store(ctx, loc, t.address + (uint64_t)A, P, o->path);
					else
						macho_lo12_store(loc, t.address + (uint64_t)A);
					break;
				case R_GOT_PAGE21:
				case R_GOT_PAGEOFF12: {
					if (!pass) {
						if (t.is_import)
							macho_import_index(ctx, t.global), macho_got_slot_index(ctx, t.global);
						break;
					}
					if (!t.is_import) {
						/* relax: ADRP to the symbol, LDR -> ADD (ld64) */
						if (type == R_GOT_PAGE21) {
							macho_page21_store(ctx, loc, t.address, P, o->path);
						} else {
							uint32_t ins = macho_u32_load(loc);
							if ((ins & 0xffc00000u) != 0xf9400000u) {
								macho_error_report(ctx, "%s: GOT_LOAD_PAGEOFF12 not on a 64-bit LDR", o->path);
								break;
							}
							uint32_t rt = ins & 0x1f, rn = (ins >> 5) & 0x1f;
							macho_u32_store(loc, 0x91000000u | rt | rn << 5 | (uint32_t)(t.address & 0xfff) << 10);
						}
						break;
					}
					uint64_t S = ctx->output_sections[ctx->got_section].address + 8ull * ctx->globals[t.global].got_slot;
					if (type == R_GOT_PAGE21)
						macho_page21_store(ctx, loc, S, P, o->path);
					else
						macho_lo12_store(loc, S);
					break;
				}
				default:
					if (!pass)
						macho_error_report(ctx, "%s: unsupported relocation type %u in %.16s", o->path, type, x->section_name);
				}
			}
		}
	}
}

/* ---------------- output helpers ---------------- */

typedef struct { uint8_t *bytes; size_t length, capacity; } macho_buffer;
static void macho_buffer_append(struct macho_context *ctx, macho_buffer *b, const void *d, size_t n)
{
	if (b->length + n > b->capacity) {
		b->capacity = (b->length + n) * 2 + 256;
		b->bytes = realloc(b->bytes, b->capacity);
		if (!b->bytes) {
			fputs("vld: out of memory\n", ctx->diagnostics.error_stream);
			exit(1);
		}
	}
	memcpy(b->bytes + b->length, d, n);
	b->length += n;
}
static void macho_buffer_append_u32(struct macho_context *ctx, macho_buffer *b, uint32_t v) { macho_buffer_append(ctx, b, &v, 4); }
static void macho_buffer_append_u64(struct macho_context *ctx, macho_buffer *b, uint64_t v) { macho_buffer_append(ctx, b, &v, 8); }
static void macho_buffer_append_u16(struct macho_context *ctx, macho_buffer *b, uint16_t v) { macho_buffer_append(ctx, b, &v, 2); }
static void macho_buffer_pad(struct macho_context *ctx, macho_buffer *b, size_t a) { static const uint8_t z[16]; while (b->length % a) macho_buffer_append(ctx, b, z, 1); }

static int macho_fixup_cmp(const void *a, const void *b)
{
	uint64_t x = ((const macho_fixup *)a)->address, y = ((const macho_fixup *)b)->address;
	return (x > y) - (x < y);
}
typedef struct { const char *name; uint32_t id; } macho_named_global;
static int macho_named_global_cmp(const void *a, const void *b)
{
	return strcmp(((const macho_named_global *)a)->name, ((const macho_named_global *)b)->name);
}
/* the global ids in ids[0..n) in the order of their names (unique, so any sort gives one order) */
static void macho_global_ids_sort_by_name(struct macho_context *ctx, uint32_t *ids, uint32_t n)
{
	macho_named_global *v = macho_alloc(ctx, (size_t)n * sizeof *v);
	for (uint32_t i = 0; i < n; i++)
		v[i] = (macho_named_global){ ctx->globals[ids[i]].name, ids[i] };
	qsort(v, n, sizeof *v, macho_named_global_cmp);
	for (uint32_t i = 0; i < n; i++)
		ids[i] = v[i].id;
	free(v);
}

typedef struct { const char *name; uint64_t address; uint8_t type, section; uint16_t description; } macho_output_symbol;

/* ---------------- driver ---------------- */

/* the end of a link: the context and everything it owns */
static void macho_context_destroy(struct macho_context *ctx)
{
	for (uint32_t i = 0; i < ctx->object_count; i++) {
		free(ctx->objects[i].file_bytes);
		free(ctx->objects[i].sections);
		free(ctx->objects[i].output_sections);
		free(ctx->objects[i].section_offsets);
		free(ctx->objects[i].global_ids);
	}
	free(ctx->objects);
	free(ctx->output_sections);
	free(ctx->globals);
	free(ctx->global_hash_slots);
	free(ctx->fixups);
	free(ctx->imports);
	free(ctx->got_globals);
	free(ctx->stub_globals);
	free(ctx);
}

static uint32_t macho_section_index_find(uint32_t osi, const uint32_t *order, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (order[i] == osi)
			return i + 1;
	return 0;
}

/* one link; the state is the link's own (struct macho_context), so vccd's workers can run
   several at once; only the libSystem export table is shared (macho_libsystem_load) */
int macho_link(int argc, char **argv, int dirfd, FILE *err)
{
	struct macho_context *ctx = calloc(1, sizeof *ctx);
	if (!ctx) {
		fputs("vld: out of memory\n", err ? err : stderr);
		return 1;
	}
	ctx->diagnostics = (struct ld_diagnostics){ .error_stream = err ? err : stderr, .program = "vld", .error_limit = 20 };
	ctx->text_section = ctx->stubs_section = ctx->got_section = MACHO_NONE;
	const char *out = "a.out";
	const char **inputs = macho_alloc(ctx, (size_t)argc * sizeof(char *));
	uint32_t ninputs = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-o") && i + 1 < argc) { out = argv[++i]; continue; }
		if (!strncmp(a, "-l", 2)) {
			const char *l = a + 2;
			if (strcmp(l, "c") && strcmp(l, "m") && strcmp(l, "pthread") && strcmp(l, "dl") && strcmp(l, "System"))
				macho_error_report(ctx, "library -l%s is not supported (only libSystem)", l);
			continue;
		}
		if (!strcmp(a, "-arch") && i + 1 < argc) {
			if (strcmp(argv[++i], "arm64"))
				macho_error_report(ctx, "-arch %s is not supported", argv[i]);
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
			macho_error_report(ctx, "unknown argument: %s", a);
			continue;
		}
		inputs[ninputs++] = a;
	}
	if (!ninputs)
		macho_error_report(ctx, "no input files");
	if (atomic_load(&ctx->diagnostics.error_count))
		goto fail;

	/* read inputs */
	ctx->objects = macho_alloc(ctx, ninputs * sizeof(macho_object));
	for (uint32_t i = 0; i < ninputs; i++) {
		macho_object *o = &ctx->objects[ctx->object_count++];
		o->path = inputs[i];
		int fd = openat(dirfd, inputs[i], O_RDONLY | O_CLOEXEC);
		struct stat st;
		if (fd < 0 || fstat(fd, &st)) {
			macho_error_report(ctx, "cannot open %s: %s", inputs[i], strerror(errno));
			if (fd >= 0)
				close(fd);
			continue;
		}
		o->size = (size_t)st.st_size;
		o->file_bytes = macho_alloc(ctx, o->size + 8);
		size_t got = 0;
		while (got < o->size) {
			ssize_t r = read(fd, o->file_bytes + got, o->size - got);
			if (r <= 0)
				break;
			got += (size_t)r;
		}
		close(fd);
		if (got != o->size || !macho_object_read(ctx, o))
			macho_error_report(ctx, "%s: cannot read", inputs[i]);
	}
	if (atomic_load(&ctx->diagnostics.error_count))
		goto fail;

	/* output sections: __text and __stubs first in __TEXT, __got alone in __DATA_CONST */
	ctx->text_section = macho_output_section_get(ctx, "__TEXT", "__text", S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS);
	ctx->stubs_section = macho_output_section_get(ctx, "__TEXT", "__stubs", S_SYMBOL_STUBS | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS);
	ctx->output_sections[ctx->stubs_section].align = 4;
	ctx->got_section = macho_output_section_get(ctx, "__DATA_CONST", "__got", S_NON_LAZY_SYMBOL_POINTERS);
	ctx->output_sections[ctx->got_section].align = 8;
	uint32_t minos = 0;
	for (uint32_t oi = 0; oi < ctx->object_count; oi++) {
		macho_object *o = &ctx->objects[oi];
		if (o->minimum_os_version > minos)
			minos = o->minimum_os_version;
		o->output_sections = macho_alloc(ctx, (o->section_count + 1) * sizeof(uint32_t));
		o->section_offsets = macho_alloc(ctx, (o->section_count + 1) * sizeof(uint64_t));
		for (uint32_t s = 0; s < o->section_count; s++) {
			macho_section_header *x = &o->sections[s];
			uint32_t t = x->flags & 0xff;
			o->output_sections[s] = MACHO_NONE;
			if ((x->flags & S_ATTR_DEBUG) || !strncmp(x->segment_name, "__LD", 16) || !strncmp(x->segment_name, "__DWARF", 16))
				continue;
			if (t == S_THREAD_LOCAL_REGULAR || t == S_THREAD_LOCAL_ZEROFILL || t == S_THREAD_LOCAL_VARIABLES) {
				macho_error_report(ctx, "%s: thread-local section %.16s is not supported", o->path, x->section_name);
				continue;
			}
			if (strncmp(x->segment_name, "__TEXT", 16) && strncmp(x->segment_name, "__DATA", 16)) {
				macho_error_report(ctx, "%s: section %.16s,%.16s is not supported", o->path, x->segment_name, x->section_name);
				continue;
			}
			uint32_t osi = macho_output_section_get(ctx, x->segment_name, x->section_name, x->flags);
			macho_output_section *os = &ctx->output_sections[osi];
			uint32_t al = x->align < 16 ? 1u << x->align : 1u << 15;
			if (al > os->align)
				os->align = al;
			os->size = macho_value_align_up(os->size, al);
			o->output_sections[s] = osi;
			o->section_offsets[s] = os->size;
			os->size += x->size;
		}
	}

	/* symbols */
	ctx->global_hash_mask = 1023;
	ctx->global_hash_slots = macho_alloc(ctx, (ctx->global_hash_mask + 1) * sizeof(uint32_t));
	for (uint32_t oi = 0; oi < ctx->object_count; oi++) {
		macho_object *o = &ctx->objects[oi];
		o->global_ids = macho_alloc(ctx, (o->symbol_count + 1) * sizeof(uint32_t));
		for (uint32_t i = 0; i < o->symbol_count; i++) {
			const macho_nlist *n = &o->symbols[i];
			o->global_ids[i] = MACHO_NONE;
			if ((n->type & N_STAB) || !(n->type & N_EXT))
				continue;
			uint32_t g = macho_global_find_or_add(ctx, macho_object_symbol_name(o, i), true);
			o->global_ids[i] = g;
			macho_global_symbol *x = &ctx->globals[g];
			uint8_t ty = n->type & N_TYPE;
			if (ty == N_UNDF && n->value) {	/* common */
				if (x->object == MACHO_NONE) {
					x->common = true;
					if (n->value > x->common_size)
						x->common_size = n->value;
					uint32_t al = 1u << ((n->description >> 8) & 0xf);
					if (al > x->common_alignment)
						x->common_alignment = al;
				}
				continue;
			}
			if (ty == N_UNDF) {
				if (x->first_reference_object == MACHO_NONE)
					x->first_reference_object = oi;
				continue;
			}
			bool weak = n->description & N_WEAK_DEF;
			if (x->object != MACHO_NONE) {
				if (x->weak && !weak) {
					x->object = oi, x->symbol = i, x->weak = false;
				} else if (!weak && !x->weak) {
					macho_error_report(ctx, "duplicate symbol: %s\n>>> defined in %s\n>>> defined in %s", x->name,
					      ctx->objects[x->object].path, o->path);
				}
				continue;
			}
			x->object = oi;
			x->symbol = i;
			x->weak = weak;
			x->common = false;
		}
	}
	/* commons into __DATA,__common */
	for (uint32_t g = 0; g < ctx->global_count; g++)
		if (ctx->globals[g].common && ctx->globals[g].object == MACHO_NONE) {
			uint32_t osi = macho_output_section_get(ctx, "__DATA", "__common", S_ZEROFILL);
			macho_output_section *os = &ctx->output_sections[osi];
			uint32_t al = ctx->globals[g].common_alignment ? ctx->globals[g].common_alignment : 1;
			if (al > os->align)
				os->align = al;
			os->size = macho_value_align_up(os->size, al);
			ctx->globals[g].address = os->size;	/* offset until layout */
			os->size += ctx->globals[g].common_size;
		}
	/* undefined -> imports from libSystem */
	bool need_sys = false;
	for (uint32_t g = 0; g < ctx->global_count; g++)
		if (ctx->globals[g].object == MACHO_NONE && !ctx->globals[g].common)
			need_sys = true;
	if (need_sys && !macho_libsystem_load(ctx))
		goto fail;
	if (!macho_sdk_version)
		macho_libsystem_load(ctx);
	for (uint32_t g = 0; g < ctx->global_count; g++) {
		macho_global_symbol *x = &ctx->globals[g];
		if (x->object != MACHO_NONE || x->common)
			continue;
		if (!macho_libsystem_is_exported(x->name)) {
			macho_error_report(ctx, "undefined symbol: %s\n>>> referenced by %s", x->name,
			      x->first_reference_object != MACHO_NONE ? ctx->objects[x->first_reference_object].path : "?");
			continue;
		}
		macho_import_index(ctx, g);
	}
	uint32_t gmain = macho_global_find_or_add(ctx, "_main", false);
	if (gmain == MACHO_NONE || ctx->globals[gmain].object == MACHO_NONE)
		macho_error_report(ctx, "undefined symbol: _main");
	if (atomic_load(&ctx->diagnostics.error_count))
		goto fail;

	/* scan relocations: stubs, GOT slots, pointer fixups (addresses come later) */
	/* (pass 0 needs no addresses except for fixups, which are redone after layout) */
	macho_relocations_process(ctx, 0, NULL);
	if (atomic_load(&ctx->diagnostics.error_count))
		goto fail;
	ctx->output_sections[ctx->stubs_section].size = 12ull * ctx->stub_count;
	ctx->output_sections[ctx->got_section].size = 8ull * ctx->got_count;

	/* output order: per segment, __text/__stubs first, zerofill last */
	uint32_t *order = macho_alloc(ctx, (ctx->output_section_count + 1) * sizeof(uint32_t)), nord = 0;
	for (int seg = 0; seg < SEG_LINKEDIT; seg++)
		for (int zf = 0; zf < 2; zf++)
			for (uint32_t i = 0; i < ctx->output_section_count; i++)
				if (ctx->output_sections[i].segment_index == seg && ctx->output_sections[i].zerofill == zf && ctx->output_sections[i].size)
					order[nord++] = i;

	/* load commands: sizes first */
	const char *dylinker = "/usr/lib/dyld", *libsys = "/usr/lib/libSystem.B.dylib";
	uint32_t nsegsecs[MACHO_SEGMENT_COUNT] = {0};
	for (uint32_t i = 0; i < nord; i++)
		nsegsecs[ctx->output_sections[order[i]].segment_index]++;
	bool segpresent[MACHO_SEGMENT_COUNT] = { true, nsegsecs[SEG_DATA_CONST] > 0, nsegsecs[SEG_DATA] > 0, true };
	uint32_t nsegs = 1;	/* __PAGEZERO */
	uint32_t sizeofcmds = 72;
	for (int s = 0; s < MACHO_SEGMENT_COUNT; s++)
		if (segpresent[s]) {
			nsegs++;
			sizeofcmds += 72 + 80 * nsegsecs[s];
		}
	uint32_t dylinker_sz = (uint32_t)macho_value_align_up(12 + strlen(dylinker) + 1, 8);
	uint32_t libsys_sz = (uint32_t)macho_value_align_up(24 + strlen(libsys) + 1, 8);
	sizeofcmds += 16 + 16 + 24 + 80 + dylinker_sz + 24 + 24 + 16 + 24 + libsys_sz + 16;
	/* chained fixups, exports trie, symtab, dysymtab, dylinker, uuid, build version,
	 * source version, main, load dylib, code signature */
	uint32_t ncmds = nsegs + 11;

	/* layout */
	uint64_t addr = MACHO_IMAGE_BASE + 32 + sizeofcmds, segstart[MACHO_SEGMENT_COUNT], segend[MACHO_SEGMENT_COUNT], segfile[MACHO_SEGMENT_COUNT], segfilesz[MACHO_SEGMENT_COUNT];
	uint64_t fileoff = 0;
	segstart[SEG_TEXT] = MACHO_IMAGE_BASE;
	segfile[SEG_TEXT] = 0;
	for (int seg = 0; seg < SEG_LINKEDIT; seg++) {
		if (seg > SEG_TEXT) {
			segstart[seg] = addr = macho_value_align_up(addr, MACHO_PAGE_SIZE);
			segfile[seg] = fileoff = macho_value_align_up(fileoff, MACHO_PAGE_SIZE);
		}
		uint64_t fend = segstart[seg];
		for (uint32_t i = 0; i < nord; i++) {
			macho_output_section *os = &ctx->output_sections[order[i]];
			if (os->segment_index != seg)
				continue;
			addr = macho_value_align_up(addr, os->align);
			os->address = addr;
			os->file_offset = os->zerofill ? 0 : segfile[seg] + (addr - segstart[seg]);
			addr += os->size;
			if (!os->zerofill)
				fend = addr;
		}
		segend[seg] = macho_value_align_up(addr, MACHO_PAGE_SIZE);
		if (seg == SEG_TEXT) {
			segend[seg] = macho_value_align_up(addr, MACHO_PAGE_SIZE);
			segfilesz[seg] = segend[seg] - segstart[seg];
		} else {
			segfilesz[seg] = segpresent[seg] && fend > segstart[seg] ? macho_value_align_up(fend - segstart[seg], MACHO_PAGE_SIZE) : 0;
		}
		if (seg == SEG_TEXT)
			fileoff = segfilesz[seg];
		else if (!segfilesz[seg])
			segfile[seg] = 0;	/* no bytes in the file: the next segment starts where this one would have */
		else
			fileoff = segfile[seg] + segfilesz[seg];
		if (!segpresent[seg]) {
			segend[seg] = segstart[seg];
			addr = segstart[seg];
		}
	}
	segstart[SEG_LINKEDIT] = macho_value_align_up(addr, MACHO_PAGE_SIZE);
	segfile[SEG_LINKEDIT] = macho_value_align_up(fileoff, MACHO_PAGE_SIZE);

	/* final symbol addresses */
	for (uint32_t g = 0; g < ctx->global_count; g++) {
		macho_global_symbol *x = &ctx->globals[g];
		if (x->object != MACHO_NONE)
			x->address = macho_local_symbol_address(ctx, &ctx->objects[x->object], x->symbol);
		else if (x->common)
			x->address += ctx->output_sections[macho_output_section_get(ctx, "__DATA", "__common", S_ZEROFILL)].address;
	}
	/* fixups again, now with addresses: GOT slots and data pointers */
	ctx->fixup_count = 0;
	{
		uint32_t keep_ngot = ctx->got_count, keep_nstub = ctx->stub_count;
		macho_relocations_process(ctx, 0, NULL);	/* slots are idempotent; fixups recorded with final addresses */
		if (ctx->got_count != keep_ngot || ctx->stub_count != keep_nstub)
			macho_error_report(ctx, "internal: slot count changed");
	}
	for (uint32_t k = 0; k < ctx->got_count; k++) {
		uint32_t g = ctx->got_globals[k];
		uint64_t at = ctx->output_sections[ctx->got_section].address + 8ull * k;
		if (ctx->globals[g].import_index != MACHO_NONE)
			macho_fixup_add(ctx, at, 1ull << 63 | ctx->globals[g].import_index);
		else
			macho_fixup_add(ctx, at, ctx->globals[g].address);
	}
	qsort(ctx->fixups, ctx->fixup_count, sizeof(macho_fixup), macho_fixup_cmp);
	for (uint32_t k = 0; k < ctx->fixup_count; k++) {
		if (ctx->fixups[k].address & 7)
			macho_error_report(ctx, "pointer fixup at 0x%llx is not 8-byte aligned", (unsigned long long)ctx->fixups[k].address);
		if (!(ctx->fixups[k].value >> 63) && (ctx->fixups[k].value < MACHO_IMAGE_BASE || ctx->fixups[k].value - MACHO_IMAGE_BASE >= (1ull << 36)))
			macho_error_report(ctx, "pointer fixup target 0x%llx out of range", (unsigned long long)ctx->fixups[k].value);
	}
	if (atomic_load(&ctx->diagnostics.error_count))
		goto fail;

	/* ---- __LINKEDIT ---- */
	macho_buffer le = {0};
	/* chained fixups */
	uint64_t cf_off = 0;
	{
		macho_buffer cf = {0};
		uint32_t segidx[MACHO_SEGMENT_COUNT + 1];	/* LC_SEGMENT ordinal of each of our segments */
		uint32_t si = 1;
		for (int s = 0; s < MACHO_SEGMENT_COUNT; s++)
			segidx[s] = segpresent[s] ? si++ : MACHO_NONE;
		/* header (28 bytes), padded to 32 */
		for (int i = 0; i < 8; i++)
			macho_buffer_append_u32(ctx, &cf, 0);
		uint32_t starts_off = (uint32_t)cf.length;
		uint32_t seg_count = nsegs;
		macho_buffer_append_u32(ctx, &cf, seg_count);
		size_t seginfo_at = cf.length;
		for (uint32_t i = 0; i < seg_count; i++)
			macho_buffer_append_u32(ctx, &cf, 0);
		for (int s = SEG_DATA_CONST; s <= SEG_DATA; s++) {
			if (!segpresent[s])
				continue;
			uint64_t lo = segstart[s], hi = segend[s];
			uint32_t first = 0;
			while (first < ctx->fixup_count && ctx->fixups[first].address < lo)
				first++;
			if (first == ctx->fixup_count || ctx->fixups[first].address >= hi)
				continue;
			macho_buffer_pad(ctx, &cf, 4);
			uint32_t info_off = (uint32_t)cf.length - starts_off;
			memcpy(cf.bytes + seginfo_at + 4 * segidx[s], &info_off, 4);
			uint16_t pages = (uint16_t)((hi - lo) / MACHO_PAGE_SIZE);
			uint32_t sz = 22 + 2 * pages;
			macho_buffer_append_u32(ctx, &cf, sz);
			macho_buffer_append_u16(ctx, &cf, (uint16_t)MACHO_PAGE_SIZE);
			macho_buffer_append_u16(ctx, &cf, 6);	/* DYLD_CHAINED_PTR_64_OFFSET */
			macho_buffer_append_u64(ctx, &cf, lo - MACHO_IMAGE_BASE);
			macho_buffer_append_u32(ctx, &cf, 0);
			macho_buffer_append_u16(ctx, &cf, pages);
			for (uint16_t p = 0; p < pages; p++) {
				uint16_t st = 0xffff;
				for (uint32_t k = first; k < ctx->fixup_count && ctx->fixups[k].address < hi; k++)
					if ((ctx->fixups[k].address - lo) / MACHO_PAGE_SIZE == p) {
						st = (uint16_t)((ctx->fixups[k].address - lo) % MACHO_PAGE_SIZE);
						break;
					}
				macho_buffer_append_u16(ctx, &cf, st);
			}
		}
		macho_buffer_pad(ctx, &cf, 4);
		uint32_t imports_off = (uint32_t)cf.length;
		macho_buffer names = {0};
		macho_buffer_append(ctx, &names, "", 1);
		for (uint32_t i = 0; i < ctx->import_count; i++) {
			uint32_t no = (uint32_t)names.length;
			macho_buffer_append(ctx, &names, ctx->globals[ctx->imports[i]].name, strlen(ctx->globals[ctx->imports[i]].name) + 1);
			macho_buffer_append_u32(ctx, &cf, 1u | no << 9);	/* lib ordinal 1 (libSystem), not weak */
		}
		uint32_t symbols_off = (uint32_t)cf.length;
		macho_buffer_append(ctx, &cf, names.bytes, names.length);
		free(names.bytes);
		macho_buffer_pad(ctx, &cf, 8);
		uint32_t hdr[7] = { 0, starts_off, imports_off, symbols_off, ctx->import_count, 1, 0 };
		memcpy(cf.bytes, hdr, sizeof hdr);
		cf_off = le.length;
		macho_buffer_append(ctx, &le, cf.bytes, cf.length);
		free(cf.bytes);
	}
	uint64_t cf_size = le.length - cf_off;
	/* exports trie: __mh_execute_header at offset 0 */
	uint64_t ex_off = le.length;
	{
		static const char nm[] = "__mh_execute_header";
		uint8_t root[2 + sizeof nm + 1];
		root[0] = 0;
		root[1] = 1;
		memcpy(root + 2, nm, sizeof nm);
		root[2 + sizeof nm] = (uint8_t)(2 + sizeof nm + 1);	/* child offset (uleb, < 128) */
		macho_buffer_append(ctx, &le, root, sizeof root);
		uint8_t child[4] = { 2, 0, 0, 0 };	/* terminal: flags 0, address 0; no children */
		macho_buffer_append(ctx, &le, child, sizeof child);
		macho_buffer_pad(ctx, &le, 8);
	}
	uint64_t ex_size = le.length - ex_off;

	/* symbol table: locals, external definitions, imports */
	macho_output_symbol *syms = macho_alloc(ctx, (ctx->global_count + 1 + 64) * sizeof(macho_output_symbol));
	uint32_t nsyms = 0, nlocal = 0, nextdef = 0, nundef = 0;
	uint32_t *sorted = macho_alloc(ctx, (ctx->global_count + 1) * sizeof(uint32_t)), ns = 0;
	for (uint32_t oi = 0; oi < ctx->object_count; oi++) {
		macho_object *o = &ctx->objects[oi];
		for (uint32_t i = 0; i < o->symbol_count; i++) {
			const macho_nlist *n = &o->symbols[i];
			if ((n->type & N_STAB) || (n->type & N_EXT) || (n->type & N_TYPE) != N_SECT)
				continue;
			const char *nm = macho_object_symbol_name(o, i);
			if (nm[0] == 'l' || nm[0] == 'L' || !nm[0])
				continue;
			uint32_t s = n->section_number - 1u;
			if (s >= o->section_count || o->output_sections[s] == MACHO_NONE)
				continue;
			syms = realloc(syms, (nsyms + ctx->global_count + 2) * sizeof(macho_output_symbol));
			syms[nsyms++] = (macho_output_symbol){nm, macho_local_symbol_address(ctx, o, i), N_SECT, (uint8_t)macho_section_index_find(o->output_sections[s], order, nord), 0};
			nlocal++;
		}
	}
	syms = realloc(syms, (nsyms + ctx->global_count + 2) * sizeof(macho_output_symbol));
	syms[nsyms++] = (macho_output_symbol){"__mh_execute_header", MACHO_IMAGE_BASE, N_SECT | N_EXT, 1, 0x10};
	nextdef++;
	for (uint32_t g = 0; g < ctx->global_count; g++)
		if (ctx->globals[g].object != MACHO_NONE || ctx->globals[g].common)
			sorted[ns++] = g;
	macho_global_ids_sort_by_name(ctx, sorted, ns);
	for (uint32_t k = 0; k < ns; k++) {
		macho_global_symbol *x = &ctx->globals[sorted[k]];
		uint32_t osi = MACHO_NONE;
		if (x->object != MACHO_NONE) {
			const macho_object *o = &ctx->objects[x->object];
			const macho_nlist *n = &o->symbols[x->symbol];
			if ((n->type & N_TYPE) == N_SECT && n->section_number - 1u < o->section_count)
				osi = o->output_sections[n->section_number - 1u];
		} else {
			osi = macho_output_section_get(ctx, "__DATA", "__common", S_ZEROFILL);
		}
		syms[nsyms++] = (macho_output_symbol){x->name, x->address, N_SECT | N_EXT,
					 (uint8_t)(osi == MACHO_NONE ? 0 : macho_section_index_find(osi, order, nord)), 0};
		nextdef++;
	}
	/* "__mh_execute_header" sorts before user names in strcmp order only by chance: keep ld64's sort */
	{
		macho_output_symbol *e = syms + nlocal;
		for (uint32_t a = 1; a < nextdef; a++) {
			macho_output_symbol t = e[a];
			uint32_t b = a;
			while (b > 0 && strcmp(e[b - 1].name, t.name) > 0) {
				e[b] = e[b - 1];
				b--;
			}
			e[b] = t;
		}
	}
	ns = 0;
	for (uint32_t i = 0; i < ctx->import_count; i++)
		sorted[ns++] = ctx->imports[i];
	macho_global_ids_sort_by_name(ctx, sorted, ns);
	uint32_t undef0 = nsyms;
	for (uint32_t k = 0; k < ns; k++) {
		syms[nsyms++] = (macho_output_symbol){ctx->globals[sorted[k]].name, 0, N_UNDF | N_EXT, 0, 1u << 8};
		ctx->globals[sorted[k]].common_alignment = nsyms - 1;	/* reused: symbol index of the import */
		nundef++;
	}
	(void)undef0;
	macho_buffer strt = {0};
	macho_buffer_append(ctx, &strt, " ", 2);
	macho_buffer_pad(ctx, &le, 8);
	uint64_t sym_off = le.length;
	for (uint32_t i = 0; i < nsyms; i++) {
		macho_nlist n = { (uint32_t)strt.length, syms[i].type, syms[i].section, syms[i].description, syms[i].address };
		macho_buffer_append(ctx, &strt, syms[i].name, strlen(syms[i].name) + 1);
		macho_buffer_append(ctx, &le, &n, sizeof n);
	}
	/* indirect symbols: __stubs then __got */
	uint64_t ind_off = le.length;
	for (uint32_t k = 0; k < ctx->stub_count; k++)
		macho_buffer_append_u32(ctx, &le, ctx->globals[ctx->stub_globals[k]].common_alignment);
	for (uint32_t k = 0; k < ctx->got_count; k++)
		macho_buffer_append_u32(ctx, &le, ctx->globals[ctx->got_globals[k]].import_index != MACHO_NONE ? ctx->globals[ctx->got_globals[k]].common_alignment : 0x80000000u);
	uint32_t nind = ctx->stub_count + ctx->got_count;
	macho_buffer_pad(ctx, &le, 8);	/* dyld wants the string pool 8-aligned */
	uint64_t str_off = le.length;
	macho_buffer_pad(ctx, &strt, 8);
	macho_buffer_append(ctx, &le, strt.bytes, strt.length);
	free(strt.bytes);
	uint64_t str_size = le.length - str_off;
	macho_buffer_pad(ctx, &le, 16);
	uint64_t sig_off = le.length;

	/* code signature size */
	const char *ident = strrchr(out, '/') ? strrchr(out, '/') + 1 : out;
	uint64_t code_limit = segfile[SEG_LINKEDIT] + sig_off;
	uint32_t nslots = (uint32_t)((code_limit + 4095) / 4096);
	uint32_t cd_size = 88 + (uint32_t)strlen(ident) + 1 + 32 * nslots;
	uint32_t sig_size = 20 + cd_size;
	uint64_t le_size = sig_off + sig_size;
	uint64_t filesize = segfile[SEG_LINKEDIT] + le_size;

	/* ---- the image ---- */
	uint8_t *img = macho_alloc(ctx, filesize);
	macho_buffer lc = {0};
	{
		uint32_t mh[8] = { MH_MAGIC_64, CPU_ARM64, 0, MH_EXECUTE, ncmds, sizeofcmds,
				   0x1 | 0x4 | 0x80 | 0x200000, 0 };	/* NOUNDEFS DYLDLINK TWOLEVEL PIE */
		macho_buffer_append(ctx, &lc, mh, sizeof mh);
	}
#define MACHO_SEGMENT_COMMAND_WRITE(name, vmaddr, vmsize, fo, fs, maxp, initp, nsec, fl) do { \
		char sn[16] = {0}; strncpy(sn, name, 16); \
		macho_buffer_append_u32(ctx, &lc, LC_SEGMENT_64); macho_buffer_append_u32(ctx, &lc, 72 + 80 * (nsec)); macho_buffer_append(ctx, &lc, sn, 16); \
		macho_buffer_append_u64(ctx, &lc, vmaddr); macho_buffer_append_u64(ctx, &lc, vmsize); macho_buffer_append_u64(ctx, &lc, fo); macho_buffer_append_u64(ctx, &lc, fs); \
		macho_buffer_append_u32(ctx, &lc, maxp); macho_buffer_append_u32(ctx, &lc, initp); macho_buffer_append_u32(ctx, &lc, nsec); macho_buffer_append_u32(ctx, &lc, fl); } while (0)
	MACHO_SEGMENT_COMMAND_WRITE("__PAGEZERO", 0, MACHO_IMAGE_BASE, 0, 0, 0, 0, 0, 0);
	static const char *segnames[MACHO_SEGMENT_COUNT] = { "__TEXT", "__DATA_CONST", "__DATA", "__LINKEDIT" };
	for (int s = 0; s < SEG_LINKEDIT; s++) {
		if (!segpresent[s])
			continue;
		uint32_t prot = s == SEG_TEXT ? 5 : 3;
		MACHO_SEGMENT_COMMAND_WRITE(segnames[s], segstart[s], segend[s] - segstart[s], segfile[s], segfilesz[s], prot, prot,
		       nsegsecs[s], s == SEG_DATA_CONST ? 0x10u : 0u);
		for (uint32_t i = 0; i < nord; i++) {
			macho_output_section *os = &ctx->output_sections[order[i]];
			if (os->segment_index != s)
				continue;
			macho_buffer_append(ctx, &lc, os->name, 16);
			macho_buffer_append(ctx, &lc, os->segment_name, 16);
			macho_buffer_append_u64(ctx, &lc, os->address);
			macho_buffer_append_u64(ctx, &lc, os->size);
			macho_buffer_append_u32(ctx, &lc, (uint32_t)os->file_offset);
			uint32_t al = 0;
			while ((1u << al) < os->align)
				al++;
			macho_buffer_append_u32(ctx, &lc, al);
			macho_buffer_append_u32(ctx, &lc, 0);
			macho_buffer_append_u32(ctx, &lc, 0);
			macho_buffer_append_u32(ctx, &lc, os->flags);
			macho_buffer_append_u32(ctx, &lc, order[i] == ctx->stubs_section ? 0 : order[i] == ctx->got_section ? ctx->stub_count : 0);
			macho_buffer_append_u32(ctx, &lc, order[i] == ctx->stubs_section ? 12 : 0);
			macho_buffer_append_u32(ctx, &lc, 0);
		}
	}
	MACHO_SEGMENT_COMMAND_WRITE("__LINKEDIT", segstart[SEG_LINKEDIT], macho_value_align_up(le_size, MACHO_PAGE_SIZE), segfile[SEG_LINKEDIT], le_size, 1, 1, 0, 0);
	macho_buffer_append_u32(ctx, &lc, LC_DYLD_CHAINED_FIXUPS); macho_buffer_append_u32(ctx, &lc, 16);
	macho_buffer_append_u32(ctx, &lc, (uint32_t)(segfile[SEG_LINKEDIT] + cf_off)); macho_buffer_append_u32(ctx, &lc, (uint32_t)cf_size);
	macho_buffer_append_u32(ctx, &lc, LC_DYLD_EXPORTS_TRIE); macho_buffer_append_u32(ctx, &lc, 16);
	macho_buffer_append_u32(ctx, &lc, (uint32_t)(segfile[SEG_LINKEDIT] + ex_off)); macho_buffer_append_u32(ctx, &lc, (uint32_t)ex_size);
	macho_buffer_append_u32(ctx, &lc, LC_SYMTAB); macho_buffer_append_u32(ctx, &lc, 24);
	macho_buffer_append_u32(ctx, &lc, (uint32_t)(segfile[SEG_LINKEDIT] + sym_off)); macho_buffer_append_u32(ctx, &lc, nsyms);
	macho_buffer_append_u32(ctx, &lc, (uint32_t)(segfile[SEG_LINKEDIT] + str_off)); macho_buffer_append_u32(ctx, &lc, (uint32_t)str_size);
	{
		uint32_t d[18] = { LC_DYSYMTAB, 80, 0, nlocal, nlocal, nextdef, nlocal + nextdef, nundef,
				   0, 0, 0, 0, 0, 0, (uint32_t)(segfile[SEG_LINKEDIT] + ind_off), nind, 0, 0 };
		macho_buffer_append(ctx, &lc, d, sizeof d);
		macho_buffer_append_u32(ctx, &lc, 0); macho_buffer_append_u32(ctx, &lc, 0);
	}
	{
		size_t at = lc.length;
		macho_buffer_append_u32(ctx, &lc, LC_LOAD_DYLINKER); macho_buffer_append_u32(ctx, &lc, dylinker_sz); macho_buffer_append_u32(ctx, &lc, 12);
		macho_buffer_append(ctx, &lc, dylinker, strlen(dylinker) + 1);
		while (lc.length - at < dylinker_sz)
			macho_buffer_append(ctx, &lc, "", 1);
	}
	size_t uuid_at = lc.length + 8;
	macho_buffer_append_u32(ctx, &lc, LC_UUID); macho_buffer_append_u32(ctx, &lc, 24); macho_buffer_append_u64(ctx, &lc, 0); macho_buffer_append_u64(ctx, &lc, 0);
	macho_buffer_append_u32(ctx, &lc, LC_BUILD_VERSION); macho_buffer_append_u32(ctx, &lc, 24); macho_buffer_append_u32(ctx, &lc, 1);	/* macOS */
	macho_buffer_append_u32(ctx, &lc, minos ? minos : 0x000b0000); macho_buffer_append_u32(ctx, &lc, macho_sdk_version ? macho_sdk_version : minos); macho_buffer_append_u32(ctx, &lc, 0);
	macho_buffer_append_u32(ctx, &lc, LC_SOURCE_VERSION); macho_buffer_append_u32(ctx, &lc, 16); macho_buffer_append_u64(ctx, &lc, 0);
	macho_buffer_append_u32(ctx, &lc, LC_MAIN); macho_buffer_append_u32(ctx, &lc, 24); macho_buffer_append_u64(ctx, &lc, ctx->globals[gmain].address - MACHO_IMAGE_BASE); macho_buffer_append_u64(ctx, &lc, 0);
	{
		size_t at = lc.length;
		macho_buffer_append_u32(ctx, &lc, LC_LOAD_DYLIB); macho_buffer_append_u32(ctx, &lc, libsys_sz); macho_buffer_append_u32(ctx, &lc, 24); macho_buffer_append_u32(ctx, &lc, 2);
		macho_buffer_append_u32(ctx, &lc, macho_version_parse("1359.40.3")); macho_buffer_append_u32(ctx, &lc, 0x10000);
		macho_buffer_append(ctx, &lc, libsys, strlen(libsys) + 1);
		while (lc.length - at < libsys_sz)
			macho_buffer_append(ctx, &lc, "", 1);
	}
	macho_buffer_append_u32(ctx, &lc, LC_CODE_SIGNATURE); macho_buffer_append_u32(ctx, &lc, 16);
	macho_buffer_append_u32(ctx, &lc, (uint32_t)(segfile[SEG_LINKEDIT] + sig_off)); macho_buffer_append_u32(ctx, &lc, sig_size);
	if (lc.length != 32 + sizeofcmds)
		macho_error_report(ctx, "internal: load commands are %zu bytes, expected %u", lc.length, 32 + sizeofcmds);
	memcpy(img, lc.bytes, lc.length);
	free(lc.bytes);

	/* section contents */
	for (uint32_t oi = 0; oi < ctx->object_count; oi++) {
		macho_object *o = &ctx->objects[oi];
		for (uint32_t s = 0; s < o->section_count; s++) {
			if (o->output_sections[s] == MACHO_NONE)
				continue;
			macho_output_section *os = &ctx->output_sections[o->output_sections[s]];
			if (os->zerofill)
				continue;
			memcpy(img + os->file_offset + o->section_offsets[s], o->file_bytes + o->sections[s].offset, o->sections[s].size);
		}
	}
	/* stubs: adrp x16, slot@PAGE; ldr x16, [x16, slot@PAGEOFF]; br x16 */
	for (uint32_t k = 0; k < ctx->stub_count; k++) {
		uint8_t *p = img + ctx->output_sections[ctx->stubs_section].file_offset + 12ull * k;
		uint64_t P = ctx->output_sections[ctx->stubs_section].address + 12ull * k, S = ctx->output_sections[ctx->got_section].address + 8ull * ctx->globals[ctx->stub_globals[k]].got_slot;
		macho_u32_store(p, 0x90000010u);
		macho_page21_store(ctx, p, S, P, "<stubs>");
		macho_u32_store(p + 4, 0xf9400210u | (uint32_t)((S & 0xfff) >> 3) << 10);
		macho_u32_store(p + 8, 0xd61f0200u);
	}
	macho_relocations_process(ctx, 1, img);
	/* chained fixup words */
	for (uint32_t k = 0; k < ctx->fixup_count; k++) {
		macho_fixup *f = &ctx->fixups[k];
		uint32_t osi = MACHO_NONE;
		for (uint32_t i = 0; i < nord; i++) {
			macho_output_section *os = &ctx->output_sections[order[i]];
			if (!os->zerofill && f->address >= os->address && f->address < os->address + os->size)
				osi = order[i];
		}
		if (osi == MACHO_NONE) {
			macho_error_report(ctx, "pointer fixup at 0x%llx is not in a section with contents", (unsigned long long)f->address);
			continue;
		}
		uint64_t next = 0;
		if (k + 1 < ctx->fixup_count && (ctx->fixups[k + 1].address - MACHO_IMAGE_BASE) / MACHO_PAGE_SIZE == (f->address - MACHO_IMAGE_BASE) / MACHO_PAGE_SIZE) {
			uint64_t d = (ctx->fixups[k + 1].address - f->address) / 4;
			next = d;
		}
		uint64_t w;
		if (f->value >> 63)
			w = 1ull << 63 | next << 51 | (f->value & 0xffffff) | ((f->value >> 32) & 0xff) << 24;
		else
			w = next << 51 | ((f->value - MACHO_IMAGE_BASE) & ((1ull << 36) - 1));
		macho_u64_store(img + ctx->output_sections[osi].file_offset + (f->address - ctx->output_sections[osi].address), w);
	}
	memcpy(img + segfile[SEG_LINKEDIT], le.bytes, le.length);
	free(le.bytes);

	/* UUID: from the content before the signature */
	{
		uint64_t h1 = vdigest_xxh3(img, code_limit), h2 = vdigest_xxh3(img + code_limit / 2, code_limit - code_limit / 2);
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
		macho_u32_store_big_endian(sg, 0xfade0cc0);
		macho_u32_store_big_endian(sg + 4, sig_size);
		macho_u32_store_big_endian(sg + 8, 1);
		macho_u32_store_big_endian(sg + 12, 0);	/* CSSLOT_CODEDIRECTORY */
		macho_u32_store_big_endian(sg + 16, 20);
		uint8_t *cd = sg + 20;
		uint32_t idlen = (uint32_t)strlen(ident) + 1;
		macho_u32_store_big_endian(cd, 0xfade0c02);
		macho_u32_store_big_endian(cd + 4, cd_size);
		macho_u32_store_big_endian(cd + 8, 0x20400);
		macho_u32_store_big_endian(cd + 12, 0x20002);	/* CS_ADHOC | CS_LINKER_SIGNED */
		macho_u32_store_big_endian(cd + 16, 88 + idlen);	/* hashOffset */
		macho_u32_store_big_endian(cd + 20, 88);	/* identOffset */
		macho_u32_store_big_endian(cd + 24, 0);	/* nSpecialSlots */
		macho_u32_store_big_endian(cd + 28, nslots);
		macho_u32_store_big_endian(cd + 32, (uint32_t)code_limit);
		cd[36] = 32;		/* hashSize */
		cd[37] = 2;		/* SHA-256 */
		cd[38] = 0;		/* platform */
		cd[39] = 12;		/* 4 KiB pages */
		macho_u32_store_big_endian(cd + 40, 0);	/* spare2 */
		macho_u32_store_big_endian(cd + 44, 0);	/* scatterOffset */
		macho_u32_store_big_endian(cd + 48, 0);	/* teamOffset */
		macho_u32_store_big_endian(cd + 52, 0);	/* spare3 */
		macho_u64_store_big_endian(cd + 56, 0);	/* codeLimit64 */
		macho_u64_store_big_endian(cd + 64, segfile[SEG_TEXT]);	/* execSegBase */
		macho_u64_store_big_endian(cd + 72, segfilesz[SEG_TEXT]);	/* execSegLimit */
		macho_u64_store_big_endian(cd + 80, 1);	/* CS_EXECSEG_MAIN_BINARY */
		memcpy(cd + 88, ident, idlen);
		uint8_t *h = cd + 88 + idlen;
		for (uint32_t p = 0; p < nslots; p++) {
			uint64_t b = (uint64_t)p * 4096, n = code_limit - b < 4096 ? code_limit - b : 4096;
			vdigest_sha256(img + b, n, h + 32 * p);
		}
	}
	free(order);
	free(syms);
	free(sorted);
	if (atomic_load(&ctx->diagnostics.error_count)) {
		free(img);
		goto fail;
	}

	/* write: a fresh file (a running binary must not be overwritten in place) */
	{
		char tmp[4096];
		snprintf(tmp, sizeof tmp, "%s.vld-tmp.%d", out, (int)getpid());
		int fd = openat(dirfd, tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0777);
		if (fd < 0) {
			macho_error_report(ctx, "cannot create %s: %s", tmp, strerror(errno));
			free(img);
			goto fail;
		}
		size_t done = 0;
		while (done < filesize) {
			ssize_t w = write(fd, img + done, filesize - done);
			if (w <= 0) {
				macho_error_report(ctx, "cannot write %s: %s", tmp, strerror(errno));
				break;
			}
			done += (size_t)w;
		}
		close(fd);
		free(img);
		if (done != filesize || renameat(dirfd, tmp, dirfd, out)) {
			if (done == filesize)
				macho_error_report(ctx, "cannot rename to %s: %s", out, strerror(errno));
			unlinkat(dirfd, tmp, 0);
			goto fail;
		}
	}
	free(inputs);
	macho_context_destroy(ctx);
	return 0;
fail:
	free(inputs);
	macho_context_destroy(ctx);
	return 1;
}
