/* What the test runtime headers (rt/include, rt/include-win64) need beyond
   the MinGW-w64 CRT (UCRT), for bench/vcc-win64.py: built by clang for
   x86_64-w64-mingw32 and linked into both clang's and vcc's programs.

   - stdin/stdout/stderr: rt's stdio.h declares them as variables; UCRT has
     only __acrt_iob_func(), so a constructor sets them.
   - The printf family is mingw-w64's own (__mingw_v*printf, what
     __USE_MINGW_ANSI_STDIO=1 selects): C99 formats and the 80-bit long
     double, where UCRT's printf takes a long double as a double.  The
     scanf family and puts/putchar/fputs are UCRT's (same FILE buffers).
   - abort() exits 3 without a message box or report; an unhandled
     exception prints "rt: unhandled exception CODE" on stderr and exits
     255 (and never starts Wine's debugger, which would hang the run).
   - A few POSIX/BSD functions the tests use that UCRT lacks, weak: a test
     may define its own. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

FILE *stdin, *stdout, *stderr;

FILE *__acrt_iob_func(unsigned);
int __mingw_vfprintf(FILE *, const char *, va_list);
int __mingw_vsnprintf(char *, size_t, const char *, va_list);
int __mingw_vsprintf(char *, const char *, va_list);
unsigned _set_abort_behavior(unsigned, unsigned);
int _stricmp(const char *, const char *);

typedef struct { unsigned code, flags; void *rec, *addr; } rt_exc_record;
typedef struct { rt_exc_record *rec; void *ctx; } rt_exc_pointers;
__declspec(dllimport) void *SetUnhandledExceptionFilter(long (*)(rt_exc_pointers *));
__declspec(dllimport) unsigned SetErrorMode(unsigned);

static long rt_unhandled(rt_exc_pointers *e)
{
	fflush(stdout);
	fprintf(stderr, "rt: unhandled exception %08x at %p\n", e->rec->code, e->rec->addr);
	fflush(stderr);
	_exit(255);
}

__attribute__((constructor(101))) static void rt_init(void)
{
	stdin = __acrt_iob_func(0);
	stdout = __acrt_iob_func(1);
	stderr = __acrt_iob_func(2);
	_set_abort_behavior(0, 3);	/* no _WRITE_ABORT_MSG, no _CALL_REPORTFAULT */
	SetErrorMode(0x8003);		/* SEM_FAILCRITICALERRORS | NOGPFAULTERRORBOX | NOOPENFILEERRORBOX */
	SetUnhandledExceptionFilter(rt_unhandled);
}

void __rt_assert(const char *e, const char *f, int l, const char *fn)
{
	fprintf(stderr, "%s:%d: %s: Assertion `%s' failed.\n", f, l, fn, e);
	abort();
}

int vfprintf(FILE *f, const char *fmt, va_list ap) { return __mingw_vfprintf(f, fmt, ap); }
int vprintf(const char *fmt, va_list ap) { return __mingw_vfprintf(stdout, fmt, ap); }
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap) { return __mingw_vsnprintf(s, n, fmt, ap); }
int vsprintf(char *s, const char *fmt, va_list ap) { return __mingw_vsprintf(s, fmt, ap); }
int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vprintf(fmt, ap); va_end(ap); return r; }
int fprintf(FILE *f, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfprintf(f, fmt, ap); va_end(ap); return r; }
int sprintf(char *s, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vsprintf(s, fmt, ap); va_end(ap); return r; }
int snprintf(char *s, size_t n, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vsnprintf(s, n, fmt, ap); va_end(ap); return r; }

__attribute__((weak)) char *stpcpy(char *d, const char *s) { size_t n = strlen(s); memcpy(d, s, n + 1); return d + n; }
__attribute__((weak)) void bzero(void *p, size_t n) { memset(p, 0, n); }
__attribute__((weak)) int bcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
__attribute__((weak)) int strcasecmp(const char *a, const char *b) { return _stricmp(a, b); }
__attribute__((weak)) int ffs(int x) { return __builtin_ffs(x); }
