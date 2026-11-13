/*
 * vlink for Windows x64 (MinGW-w64): COFF x86-64 objects, archives and
 * import libraries -> a PE32+ executable or DLL.  ld_main() hands a link
 * here when its first input is a COFF x86-64 object or the command line has
 * -m i386pep (doc/PLAN-win64.md, step WIN7).
 *
 * The command line is GNU ld's as clang --target=x86_64-w64-mingw32 passes
 * it to ld.lld, and the output is meant to be byte-identical to ld.lld's
 * (lld-link in MinGW mode) for the same command line, so lld's observable
 * choices are reproduced: the order in which input files and archive
 * members are read (a FIFO of load tasks), chunk order (non-COMDAT sections
 * by index, then COMDAT leaders and COMMONs in symbol order, then
 * associative sections), partial sections sorted by full name and merged
 * into output sections (.idata/.xdata/.CRT/.ctors/.dtors/.00cfg into
 * .rdata, .bss into .data), the synthetic import tables and thunks, MinGW's
 * __CTOR_LIST__/__DTOR_LIST__ and runtime pseudo relocations, base
 * relocation blocks, sorted .pdata, the .buildid debug directory with an
 * XXH3-based build id, and the COFF symbol table lld writes for MinGW.
 *
 * One TU like ld.c: the files below are #included in order.
 *   pe-options.c    command line (lld's MinGW driver), library search
 *   pe-input.c      objects, archives, short import records, symbols
 *   pe-resolve.c    load queue, directives, the driver's resolution steps,
 *                   undefined symbols, --gc-sections
 *   pe-export.c     exports (.edata), MinGW auto-export, --out-implib
 *   pe-layout.c     partial/output sections, synthetic chunks, addresses
 *   pe-write.c      relocations, headers, symbol table, build id, -Map
 */
#include "ld/ld.h"
#include "common/digest.h"
#include "port/task.h"

#include "pe-linker-internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---------------- state of the one link that runs at a time ---------------- */

static pe_config cfg;
static FILE *pe_err_stream;
static const char *pe_prog = "vlink";
static int pe_error_count, pe_warning_count;
static int pe_dirfd = AT_FDCWD;

static pe_file **pe_objects;		/* lld's objFileInstances */
static uint32_t pe_object_count, pe_object_capacity;
static pe_file **pe_imports;		/* lld's importFileInstances */
static uint32_t pe_import_count, pe_import_capacity;
static pe_symbol **pe_globals;		/* every global, in insertion order */
static uint32_t pe_global_count, pe_global_capacity;
static pe_symbol **pe_hash;
static uint32_t pe_hash_mask;
static uint32_t pe_chunk_order;

/* ---------------- memory: one arena per link ---------------- */

typedef struct pe_arena_block { struct pe_arena_block *next; size_t used, cap; } pe_arena_block;
static pe_arena_block *pe_arena;

static void pe_oom(void)
{
	fputs("vlink: out of memory\n", stderr);
	exit(1);
}

static void *pe_alloc(size_t n)
{
	n = (n + 15) & ~(size_t)15;
	if (!pe_arena || pe_arena->used + n > pe_arena->cap) {
		size_t cap = n > (1u << 20) ? n : (1u << 20);
		pe_arena_block *b = malloc(sizeof *b + 16 + cap);
		if (!b)
			pe_oom();
		b->next = pe_arena;
		b->used = 0;
		b->cap = cap;
		pe_arena = b;
	}
	void *p = (char *)(pe_arena + 1) + 16 + pe_arena->used;
	pe_arena->used += n;
	memset(p, 0, n);
	return p;
}

static void pe_arena_free(void)
{
	while (pe_arena) {
		pe_arena_block *n = pe_arena->next;
		free(pe_arena);
		pe_arena = n;
	}
}

static char *pe_strndup(const char *s, size_t n)
{
	char *p = pe_alloc(n + 1);
	memcpy(p, s, n);
	p[n] = 0;
	return p;
}

static char *pe_strdup(const char *s) { return pe_strndup(s, strlen(s)); }

static char *pe_format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static char *pe_format(const char *fmt, ...)
{
	va_list ap, aq;
	va_start(ap, fmt);
	va_copy(aq, ap);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	char *p = pe_alloc((size_t)n + 1);
	vsnprintf(p, (size_t)n + 1, fmt, aq);
	va_end(aq);
	return p;
}

/* a growable array of pointers in malloc memory (freed with the link) */
#define PE_PUSH(v, n, cap, x) do { if ((n) >= (cap)) { (cap) = (cap) ? 2 * (cap) : 16; \
	void *pe_np_ = realloc((v), (size_t)(cap) * sizeof(*(v))); if (!pe_np_) pe_oom(); (v) = pe_np_; } \
	(v)[(n)++] = (x); } while (0)

/* ---------------- diagnostics ---------------- */

static void pe_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void pe_error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (pe_error_count++ < 20) {
		fprintf(pe_err_stream, "%s: error: ", pe_prog);
		vfprintf(pe_err_stream, fmt, ap);
		fputc('\n', pe_err_stream);
	} else if (pe_error_count == 21)
		fprintf(pe_err_stream, "%s: error: too many errors emitted, stopping now\n", pe_prog);
	va_end(ap);
}

static void pe_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void pe_warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (cfg.fatal_warnings) {
		fprintf(pe_err_stream, "%s: error: ", pe_prog);
		pe_error_count++;
	} else
		fprintf(pe_err_stream, "%s: warning: ", pe_prog);
	vfprintf(pe_err_stream, fmt, ap);
	fputc('\n', pe_err_stream);
	pe_warning_count++;
	va_end(ap);
}

static void pe_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void pe_log(const char *fmt, ...)
{
	if (!cfg.verbose)
		return;
	va_list ap;
	va_start(ap, fmt);
	fprintf(pe_err_stream, "%s: ", pe_prog);
	vfprintf(pe_err_stream, fmt, ap);
	fputc('\n', pe_err_stream);
	va_end(ap);
}

static const char *pe_basename(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

/* lld's toString(InputFile): "archive.a(member.o)" or the path */
static const char *pe_file_name(const pe_file *f)
{
	if (!f)
		return "<internal>";
	if (!f->parent)
		return f->path;
	return pe_format("%s(%s)", pe_basename(f->parent), pe_basename(f->path));
}

static inline uint64_t pe_align_up(uint64_t v, uint64_t a) { return a ? (v + a - 1) / a * a : v; }

#include "pe-options.c"
#include "pe-input.c"
#include "pe-resolve.c"
#include "pe-export.c"
#include "pe-layout.c"
#include "pe-write.c"

/* ---------------- entry ---------------- */

static void pe_state_reset(void)
{
	for (uint32_t i = 0; i < pe_object_count; i++)
		free(pe_objects[i]->chunks);
	free(cfg.exports);
	free(cfg.align_comm);
	free(cfg.alternate_names);
	free((void *)cfg.includes);
	free((void *)cfg.optional_includes);
	free((void *)cfg.exclude_libs);
	free((void *)cfg.exclude_symbols);
	free(pe_objects);
	free(pe_imports);
	free(pe_globals);
	free(pe_hash);
	pe_objects = NULL;
	pe_imports = NULL;
	pe_globals = NULL;
	pe_hash = NULL;
	pe_object_count = pe_object_capacity = pe_import_count = pe_import_capacity = 0;
	pe_global_count = pe_global_capacity = pe_hash_mask = 0;
	pe_chunk_order = 0;
	pe_error_count = pe_warning_count = 0;
	pe_input_reset();
	pe_resolve_reset();
	pe_layout_reset();
	pe_write_reset();
	pe_arena_free();
	memset(&cfg, 0, sizeof cfg);
}

static int pe_link_locked(int argc, char **argv, int dirfd, FILE *err)
{
	pe_err_stream = err ? err : stderr;
	pe_dirfd = dirfd;
	pe_prog = argc > 0 ? pe_basename(argv[0]) : "vlink";
	if (!pe_options_parse(argc, argv))
		return pe_error_count ? 1 : 0;	/* (--version, --help) */
	if (pe_error_count)
		return 1;
	pe_symbols_init();
	if (!pe_resolve_run() || pe_error_count)
		return 1;
	if (!pe_layout_run() || pe_error_count)
		return 1;
	if (!pe_write_run() || pe_error_count)
		return 1;
	return 0;
}

int pe_link(int argc, char **argv, int dirfd, FILE *err)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	vtask_mutex_lock(&lock);
	int rc = pe_link_locked(argc, argv, dirfd, err);
	pe_state_reset();
	pthread_mutex_unlock(&lock);
	return rc;
}

/* the first input is a COFF x86-64 object (or an archive of them), or -m i386pep is given */
bool pe_link_wanted(int argc, char **argv, int dirfd)
{
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-m") && i + 1 < argc)
			return !strcmp(argv[i + 1], "i386pep");
		if (!strcmp(a, "-mi386pep"))
			return true;
	}
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] == '-') {
			if (!strcmp(a, "-o") || !strcmp(a, "-T") || !strcmp(a, "-L") || !strcmp(a, "-l") ||
			    !strcmp(a, "-e") || !strcmp(a, "-u") || !strcmp(a, "-m") || !strcmp(a, "-Map") ||
			    !strcmp(a, "-z") || !strcmp(a, "--sysroot") || !strcmp(a, "-arch") || !strcmp(a, "-syslibroot"))
				i++;
			continue;
		}
		int fd = openat(dirfd, a, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			return false;
		uint8_t m[4] = {0};
		bool r = read(fd, m, 4) == 4 && m[0] == 0x64 && m[1] == 0x86;
		close(fd);
		return r;
	}
	return false;
}
