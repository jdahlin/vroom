/*
 * fuzz-pp: the preprocessor on one in-memory translation unit.
 *
 * The input is split at \x01 bytes into the main file and up to three
 * headers:
 *
 *   /fuzz/main.c \x01 /fuzz/a.h \x01 /fuzz/b.h \x01 /fuzz/sys/c.h
 *
 * /fuzz/sys is the only search directory (a system one), so "a.h", "b.h"
 * resolve next to the includer and <c.h>, "sys/c.h" through the path.  A
 * leading \x02 byte selects assembler-with-cpp mode.  Nothing touches the
 * file system: vlex_get_hook serves the files, the header cache is off.
 *
 * Oracles: ASan/UBSan, the fuzzer's timeout (hangs), and a few invariants
 * on every delivered token (spelling, location).  Output is cut off after
 * MAXOUT tokens: exponential macros are legal and clang explodes on them too.
 */
#include <fcntl.h>
#include <setjmp.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "../vcc/lib/intern.h"
#include "../vcc/lex/lex.h"
#include "../vcc/pp/pp.h"
#include "fuzz.h"

#define MAXIN (256u << 10)
#define MAXOUT 200000
#define INTERN_MAX 900000

enum { NFILE = 4 };
static const char *const names[NFILE] = { "/fuzz/main.c", "/fuzz/a.h", "/fuzz/b.h", "/fuzz/sys/c.h" };
static vlex_file files[NFILE];
static bool present[NFILE];
static int devnull, print;	/* FUZZ_PP_PRINT=1: tokens and diagnostics to stderr */

/* a few predefines in the style of ppdata-predef.inc (small: <built-in> is immortal) */
static const char predef[] =
	"#define __STDC__ 1\n"
	"#define __STDC_VERSION__ 201112L\n"
	"#define __GNUC__ 4\n"
	"#define __aarch64__ 1\n"
	"#define __CHAR_BIT__ 8\n"
	"#define __INT_MAX__ 2147483647\n"
	"#define __SIZE_TYPE__ long unsigned int\n"
	"#define __ARM_ACLE_VERSION(year,quarter,patch) (100 * (year) + 10 * (quarter) + (patch))\n"
	"#define __has_include_fuzz(x) __has_include(x)\n";

static const vlex_file *hook(const char *path, size_t len)
{
	for (int i = 0; i < NFILE; i++)
		if (present[i] && strlen(names[i]) == len && !memcmp(names[i], path, len))
			return &files[i];
	return NULL;
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc, (void)argv;
	setenv("VPP_NOHC", "1", 1);		/* the header cache keys on files: they are new every time */
	setenv("VPP_NODIRCACHE", "1", 1);	/* /fuzz does not exist */
	print = getenv("FUZZ_PP_PRINT") != NULL;
	devnull = print ? 2 : open("/dev/null", O_WRONLY);
	vintern_init();
	vlex_get_hook = hook;
	return 0;
}

/* lex [d, d + n) as file i (interning: the source must then stay) */
static char *load(int i, const uint8_t *d, size_t n, void *scr)
{
	char *s = malloc(n + 128);
	vtok *t = malloc((n + 2) * sizeof(vtok));

	memcpy(s, d, n);
	memset(s + n, 0, 128);
	files[i] = (vlex_file){ .path = names[i], .src = s, .size = (uint32_t)n };
	files[i].ntok = vlex(s, n, t, scr);
	files[i].tok = t;
	present[i] = true;
	return s;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	pp_opts o = { 0 };
	const char *dirs[] = { "/fuzz/sys" };
	char *src[NFILE] = { 0 };
	uint32_t before = vintern_count();
	jmp_buf jb;
	pp *p;
	void *scr;
	char *sp;

	if (n > MAXIN || before > INTERN_MAX)
		return 0;
	if (n && d[0] == 2) {
		o.asm_mode = 1;
		d++, n--;
	}
	scr = malloc(vlex_scratch_size(n));
	for (int i = 0; i < NFILE; i++) {
		const uint8_t *e = i < NFILE - 1 ? memchr(d, 1, n) : NULL;
		size_t l = e ? (size_t)(e - d) : n;

		src[i] = load(i, d, l, scr);
		if (!e)
			break;
		d += l + 1;
		n -= l + 1;
	}
	free(scr);

	o.main = names[0];
	o.dirs = dirs;
	o.ndirs = 1;
	o.nsys = -1;	/* (no system directory) */
	o.nquote = 0;
	o.nsys1 = 0;	/* no system directories */
	o.predef = predef;
	o.unsigned_char = 1;
	o.errfd = devnull;
	o.target = PP_LINUX_AARCH64;
	p = pp_open(&o);
	if (!p)
		goto out;	/* a fatal error in the main file */
	sp = malloc(MAXIN + 64);
	pp_set_fatal(p, &jb);
	if (!setjmp(jb)) {
		ptok t;

		for (unsigned k = 0; k < MAXOUT; k++) {
			const char *name;
			uint32_t nl, line, col;
			size_t l;

			pp_next(p, &t);
			if (t.kind == T_EOF)
				break;
			FUZZ_CHECK(t.kind <= P_DIRECTIVE, "kind %u", t.kind);
			FUZZ_CHECK(t.kind == T_IDENT ? t.id != 0 : 1, "identifier without id");
			if (t.len <= MAXIN) {
				const char *s = pp_spell(&t, sp, &l);

				FUZZ_CHECK(l <= t.len, "spelling %zu > %u", l, t.len);
				if (print)
					fprintf(stderr, "%s%.*s", t.flags & TF_SOL ? "\n" : t.flags & TF_SPACE ? " " : "", (int)l, s);
				if (t.kind == T_IDENT)
					FUZZ_CHECK(vintern_ents[t.id].len == l && !memcmp(vintern_ents[t.id].s, s, l),
						   "identifier spelling vs id %u", t.id);
			}
			(void)pp_where(p, t.loc, &name, &nl, &line, &col);	/* line may be 0: #line 0 */
		}
	}
	if (print)
		fprintf(stderr, "\n[%d errors]\n", pp_errors(p));
	pp_close(p);
	free(sp);
out:
	for (int i = 0; i < NFILE; i++) {
		if (present[i]) {
			free((void *)files[i].tok);
			if (vintern_count() == before)
				free(src[i]);
		}
		present[i] = false;
	}
	return 0;
}
