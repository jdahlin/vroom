/*
 * fuzz-diag: the diagnostic renderers (PLAN-diag.md D4, layer 8) on random
 * records over random text.
 *
 * The input is a control part and a source file, split at the first \x01:
 *
 *   control \x01 /fuzz/main.c
 *
 * The source is preprocessed to its end (the file table as cc leaves it),
 * then the control bytes, read as a little program, build records: a level,
 * a location, spans with and without labels, fix-its (insertions, deletions,
 * replacements, also multi-line ones), notes, help, a message cut from the
 * source (quotes and UTF-8 included, so the type diff and the column widths
 * see odd text).  Every location lies in the file or is "none"
 * (UINT32_MAX), as the front end's are.  The same records go through every
 * style and output: vcc, clang and narrated text in ASCII and Unicode, with
 * and without color, JSON lines and SARIF.
 *
 * Oracles: ASan/UBSan and the fuzzer's timeout.  FUZZ_DIAG_PRINT=1 prints
 * the output.
 */
#include <fcntl.h>
#include <setjmp.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "common/intern.h"
#include "frontend/lex/lex.h"
#include "frontend/pp/pp.h"
#include "fuzz.h"

#define MAXIN (64u << 10)
#define INTERN_MAX 900000
#define MAXREC 24

static const char *const name = "/fuzz/main.c";
static lex_file file;
static int devnull, print;

static const lex_file *hook(const char *path, size_t len)
{
	return strlen(name) == len && !memcmp(name, path, len) ? &file : NULL;
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc, (void)argv;
	setenv("VPP_NOHC", "1", 1);
	setenv("VPP_NODIRCACHE", "1", 1);
	print = getenv("FUZZ_DIAG_PRINT") != NULL;
	devnull = print ? 2 : open("/dev/null", O_WRONLY);
	vintern_init();
	lex_file_get_hook = hook;
	return 0;
}

/* the control program: a byte stream that runs out into zeros */
typedef struct ctl { const uint8_t *d; size_t n, i; } ctl;

static unsigned byte(ctl *c)
{
	return c->i < c->n ? c->d[c->i++] : 0;
}

static unsigned u16(ctl *c)
{
	unsigned a = byte(c);

	return a | byte(c) << 8;
}

/* a location in the file [base, base + size], or none */
static uint32_t loc(ctl *c, uint32_t base, uint32_t size)
{
	unsigned v = u16(c);

	return v == 0xffff ? UINT32_MAX : base + v % (size + 1);
}

/* a piece of the source (at most 40 bytes) as text: messages, labels, fix-its */
static const char *text(ctl *c, const char *src, uint32_t size, char *buf)
{
	unsigned o = u16(c), l = byte(c) % 41;

	if (!size)
		l = 0;
	else {
		o %= size;
		if (l > size - o)
			l = size - o;
	}
	memcpy(buf, src + o, l);
	buf[l] = 0;
	return buf;
}

/* build the records of the control program and emit them */
static void records(pp *p, const uint8_t *cd, size_t cn, const char *src, uint32_t base, uint32_t size)
{
	ctl c = { cd, cn, 0 };
	char t1[48], t2[48];

	for (int r = 0; r < MAXREC && c.i < c.n; r++) {
		unsigned op = byte(&c);
		int level = op % 5 == VDL_NOTE ? VDL_ERROR : op % 5;	/* a top-level note is a stray note */
		uint32_t at = loc(&c, base, size);
		vdiag *d = pp_diag_begin(p, level, at, "%s", text(&c, src, size, t1));

		if (op & 0x08)
			pp_diag_flag(d, "fuzz-flag");
		for (unsigned k = byte(&c) % 5; k--;) {
			uint32_t b = loc(&c, base, size), e = loc(&c, base, size);

			if (b == UINT32_MAX || e == UINT32_MAX)
				continue;
			if (e < b) {
				uint32_t x = b;

				b = e, e = x;
			}
			pp_diag_label(p, d, b, e, byte(&c) & 1 ? text(&c, src, size, t2) : NULL);
		}
		for (unsigned k = byte(&c) % 4; k--;) {
			uint32_t b = loc(&c, base, size), e = b;

			if (b == UINT32_MAX)
				continue;
			e = b + byte(&c) % 8;
			if (e > base + size)
				e = base + size;
			pp_diag_fix(p, d, b, e, text(&c, src, size, t2), byte(&c) % 4);
		}
		if (op & 0x10)
			pp_diag_help(p, d, "%s", text(&c, src, size, t2));
		for (unsigned k = byte(&c) % 4; k--;) {
			vdiag *n = pp_diag_note(p, d, loc(&c, base, size), "%s", text(&c, src, size, t2));
			uint32_t b = loc(&c, base, size);

			if (n && b != UINT32_MAX && byte(&c) & 1)
				pp_diag_label(p, n, b, b + 1 <= base + size ? b + 1 : b, NULL);
		}
		pp_diag_emit(p, d);
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const struct { int fmt, style, uni, color; } outs[] = {
		{ PP_DIAG_TEXT, PP_DSTYLE_VCC, 0, -1 },      { PP_DIAG_TEXT, PP_DSTYLE_VCC, 1, 1 },
		{ PP_DIAG_TEXT, PP_DSTYLE_CLANG, 0, -1 },    { PP_DIAG_TEXT, PP_DSTYLE_CLANG, 0, 1 },
		{ PP_DIAG_TEXT, PP_DSTYLE_NARRATED, 0, -1 }, { PP_DIAG_TEXT, PP_DSTYLE_VCC, 0, 1 },
		{ PP_DIAG_JSON, PP_DSTYLE_VCC, 0, -1 },      { PP_DIAG_SARIF, PP_DSTYLE_VCC, 1, -1 },
	};
	const uint8_t *sep = memchr(d, 1, n);
	uint32_t before = vintern_count();
	size_t cn, sn;
	char *src;
	lex_token *tk;
	void *scr;

	if (!sep || n > MAXIN || before > INTERN_MAX)
		return 0;
	cn = (size_t)(sep - d), sn = n - cn - 1;
	src = malloc(sn + 128);
	memcpy(src, sep + 1, sn);
	memset(src + sn, 0, 128);
	tk = malloc((sn + 2) * sizeof(lex_token));
	scr = malloc(lex_scratch_size(sn));
	file = (lex_file){ .path = name, .src = src, .size = (uint32_t)sn };
	file.ntok = lex_tokenize(src, sn, tk, scr);
	file.tok = tk;
	free(scr);

	for (size_t k = 0; k < sizeof outs / sizeof *outs; k++) {
		pp_opts o = { 0 };
		jmp_buf jb;
		pp *p;

		o.main = name;
		o.predef = "";
		o.unsigned_char = 1;
		o.errfd = devnull;
		o.target = PP_LINUX_AARCH64;
		o.diag_format = outs[k].fmt;
		o.diag_style = outs[k].style;
		o.diag_unicode = outs[k].uni;
		o.diag_color = outs[k].color;
		o.fatal_continue = 1;
		p = pp_open(&o);
		if (!p)
			break;
		pp_fatal_jump_set(p, &jb);
		if (!setjmp(jb)) {
			ptok t;
			uint32_t eof, base;

			do
				pp_token_next(p, &t);
			while (t.kind != LEX_TOKEN_EOF);
			eof = pp_main_eof(p);
			base = eof - (sn && src[sn - 1] == '\n' ? (uint32_t)sn - 1 : (uint32_t)sn);
			records(p, d, cn, src, base, (uint32_t)sn);
			pp_diag_summary(p);
		}
		if (print)
			fprintf(stderr, "---- output %zu ----\n", k);
		pp_close(p);
	}
	free(tk);
	if (vintern_count() == before)
		free(src);
	return 0;
}
