/*
 * vpp: the preprocessor alone, for parity tests against clang -E.
 *
 *   vpp [--cmp REF.i] [-o OUT] CLANG-ARGS...
 *
 * Takes a clang command line as kbuild writes it (-I, -iquote, -isystem,
 * -idirafter, -include, -D, -U, -O, -f/-m flags that change predefined
 * macros, -fmacro-prefix-map) and prints what clang -E -P prints, one
 * token sequence per line.  With --cmp, the output is not written but
 * compared token by token (kind and spelling, lexed by vcc's lexer) with
 * REF.i; the first difference is shown and the exit status is 1.
 *
 *   VPP_TIME=1   report the preprocessing time on stderr
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "intern.h"
#include "pp.h"

#include "ppdata-predef.inc"

typedef struct buf {
	char *p;
	size_t n, cap;
} buf;

static void bput(buf *b, const char *s, size_t n)
{
	if (b->n + n + 256 > b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 1u << 24;
		char *np;

		while (nc < b->n + n + 256)
			nc *= 2;
		np = mmap(NULL, nc, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (np == MAP_FAILED)
			abort();
		if (b->p) {
			memcpy(np, b->p, b->n);
			munmap(b->p, b->cap);
		}
		b->p = np;
		b->cap = nc;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
}

static void bstr(buf *b, const char *s) { bput(b, s, strlen(s)); }

static void define_arg(buf *b, const char *d)
{
	const char *eq = strchr(d, '=');

	bstr(b, "#define ");
	if (eq) {
		bput(b, d, (size_t)(eq - d));
		bstr(b, " ");
		bstr(b, eq + 1);
	} else {
		bstr(b, d);
		bstr(b, " 1");
	}
	bstr(b, "\n");
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* the tokens of a file (whole, zero padded), lexed */
static vlex_file lexbuf(const char *s, size_t n)
{
	vlex_file f = { 0 };
	char *src = mmap(NULL, n + 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	vtok *tok = mmap(NULL, (n + 2) * sizeof(vtok), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	void *scr = mmap(NULL, vlex_scratch_size(n) + 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

	memcpy(src, s, n);
	f.src = src;
	f.size = (uint32_t)n;
	f.ntok = vlex(src, n, tok, scr);
	f.tok = tok;
	return f;
}

static size_t clean(const vlex_file *f, const vtok *t, char *b)
{
	if (t->flags & TF_DIRTY)
		return vlex_spell(f->src, t, b);
	memcpy(b, f->src + t->off, t->len);
	return t->len;
}

static unsigned line_of(const vlex_file *f, uint32_t off)
{
	unsigned l = 1;

	for (uint32_t i = 0; i < off; i++)
		l += f->src[i] == '\n';
	return l;
}

static void show(const char *what, const vlex_file *f, uint32_t i)
{
	uint32_t a = i > 12 ? i - 12 : 0, e = i + 12 < f->ntok ? i + 12 : f->ntok;
	char b[1 << 16];

	fprintf(stderr, "%s (line %u):", what, i < f->ntok ? line_of(f, f->tok[i].off) : 0);
	for (uint32_t k = a; k < e; k++) {
		size_t n = clean(f, &f->tok[k], b);

		fprintf(stderr, k == i ? " >>>%.*s<<<" : " %.*s", (int)(n > 200 ? 200 : n), b);
	}
	fputc('\n', stderr);
}

static int compare(const char *ours, size_t on, const char *refpath)
{
	int fd = open(refpath, O_RDONLY);
	struct stat st;
	char *r;
	vlex_file A, B;
	static char ba[1 << 20], bb[1 << 20];

	if (fd < 0 || fstat(fd, &st)) {
		perror(refpath);
		return 2;
	}
	r = mmap(NULL, (size_t)st.st_size + 1, PROT_READ, MAP_PRIVATE, fd, 0);
	A = lexbuf(ours, on);
	B = lexbuf(r, (size_t)st.st_size);
	for (uint32_t i = 0;; i++) {
		const vtok *x = &A.tok[i], *y = &B.tok[i];
		size_t nx, ny;

		if (i == A.ntok || i == B.ntok) {
			if (A.ntok == B.ntok) {
				printf("same: %u tokens\n", A.ntok);
				return 0;
			}
			printf("length differs: ours %u, clang %u tokens\n", A.ntok, B.ntok);
			show("ours ", &A, i);
			show("clang", &B, i);
			return 1;
		}
		nx = clean(&A, x, ba);
		ny = clean(&B, y, bb);
		if (x->kind != y->kind || nx != ny || memcmp(ba, bb, nx)) {
			printf("token %u differs\n", i);
			show("ours ", &A, i);
			show("clang", &B, i);
			return 1;
		}
	}
}

int main(int argc, char **argv)
{
	const char *dirs_q[256], *dirs_i[1024], *dirs_s[256], *dirs_a[256], *all[2048];
	int nq = 0, ni = 0, ns = 0, na = 0, n = 0;
	const char *cmp = NULL, *out = NULL, *main_file = NULL;
	buf pre = { 0 }, cmdl = { 0 }, incl = { 0 }, o = { 0 };
	int darwin = 1, nostdinc = 0, asm_mode = 0, undef = 0;
	int opt = 2, os = 0, pie = 0, pic = 0, freestanding = 0, cfi = 0, strict_align = 0, tiny = 0;
	pp_opts po = { 0 };
	char *pmap = NULL;
	double t0;
	pp *p;
	ptok t;
	int col = 0;
	char sb[1 << 16];

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define ARG(flag) (!strcmp(a, flag) ? (i + 1 < argc ? argv[++i] : "") : !strncmp(a, flag, strlen(flag)) ? a + strlen(flag) : NULL)
		const char *v;

		if (!strcmp(a, "--cmp") && i + 1 < argc) { cmp = argv[++i]; continue; }
		if (!strcmp(a, "-o") && i + 1 < argc) { out = argv[++i]; continue; }
		if ((v = ARG("-iquote"))) { dirs_q[nq++] = v; continue; }
		if ((v = ARG("-isystem"))) { dirs_s[ns++] = v; continue; }
		if ((v = ARG("-idirafter"))) { dirs_a[na++] = v; continue; }
		if ((v = ARG("-include"))) {
			bstr(&incl, "#include \"");
			bstr(&incl, v);
			bstr(&incl, "\"\n");
			continue;
		}
		if ((v = ARG("-I"))) { dirs_i[ni++] = v; continue; }
		if ((v = ARG("-D"))) { define_arg(&cmdl, v); continue; }
		if ((v = ARG("-U"))) { bstr(&cmdl, "#undef "); bstr(&cmdl, v); bstr(&cmdl, "\n"); continue; }
		if (!strncmp(a, "-fmacro-prefix-map=", 19)) {
			pmap = strdup(a + 19);
			continue;
		}
		if (!strncmp(a, "--target=", 9) || !strcmp(a, "-target")) darwin = strstr(a, "darwin") || strstr(a, "apple");
		else if (!strcmp(a, "-nostdinc")) nostdinc = 1;
		else if (!strcmp(a, "-undef")) undef = 1;
		else if (!strcmp(a, "-x") && i + 1 < argc) asm_mode = !strcmp(argv[++i], "assembler-with-cpp");
		else if (!strcmp(a, "-O0")) opt = 0;
		else if (!strcmp(a, "-Os") || !strcmp(a, "-Oz")) { opt = 2; os = 1; }
		else if (!strncmp(a, "-O", 2)) opt = 2;
		else if (!strcmp(a, "-fpie")) pie = 1;
		else if (!strcmp(a, "-fPIE")) pie = 2;
		else if (!strcmp(a, "-fpic")) pic = 1;
		else if (!strcmp(a, "-fPIC")) pic = 2;
		else if (!strcmp(a, "-fno-PIE") || !strcmp(a, "-fno-pie")) pie = 0;
		else if (!strcmp(a, "-ffreestanding")) freestanding = 1;
		else if (!strcmp(a, "-fasynchronous-unwind-tables")) cfi = 1;
		else if (!strcmp(a, "-fno-asynchronous-unwind-tables")) cfi = 0;
		else if (!strcmp(a, "-mstrict-align")) strict_align = 1;
		else if (!strcmp(a, "-mcmodel=tiny")) tiny = 1;
		else if (a[0] != '-' && (strstr(a, ".c") || strstr(a, ".S") || strstr(a, ".dts"))) {
			size_t l = strlen(a);

			main_file = a;
			if (l > 2 && !strcmp(a + l - 2, ".S"))
				asm_mode = 1;
		}
	}
	if (!main_file) {
		fprintf(stderr, "vpp: no input file\n");
		return 2;
	}
	/* search path: -iquote, -I, -isystem, -idirafter; exact duplicates dropped */
	for (int k = 0; k < nq; k++) all[n++] = dirs_q[k];
	po.nquote = n;
#define ADD(d) do { int dup_ = 0; for (int j = po.nquote; j < n; j++) dup_ |= !strcmp(all[j], d); if (!dup_) all[n++] = d; } while (0)
	for (int k = 0; k < ni; k++) ADD(dirs_i[k]);
	for (int k = 0; k < ns; k++) ADD(dirs_s[k]);
	if (darwin && !nostdinc)
		for (size_t k = 0; k < sizeof(sysdirs_darwin) / sizeof(sysdirs_darwin[0]); k++)
			ADD(sysdirs_darwin[k]);
	for (int k = 0; k < na; k++) ADD(dirs_a[k]);
	if (darwin && !nostdinc) {
		po.fwdirs = fwdirs_darwin;
		po.nfwdirs = (int)(sizeof(fwdirs_darwin) / sizeof(fwdirs_darwin[0]));
	}
	po.dirs = all;
	po.ndirs = n;

	if (undef)
		bstr(&pre, asm_mode ? predef_undef_asm : predef_undef_c);
	else
		bstr(&pre, darwin ? predef_darwin : predef_aarch64_linux);
	if (asm_mode && !undef)
		bstr(&pre, "#define __ASSEMBLER__ 1\n#undef __STDC_VERSION__\n#define __STDC_VERSION__ 201710L\n");
	po.target = darwin ? PP_DARWIN : PP_LINUX_AARCH64;
	if (opt == 0) bstr(&pre, "#undef __OPTIMIZE__\n#define __NO_INLINE__ 1\n");
	if (os) bstr(&pre, "#define __OPTIMIZE_SIZE__ 1\n");
	if (pie || pic) {
		char b[160];
		int l = pie ? pie : pic;

		snprintf(b, sizeof(b), "#define __PIC__ %d\n#define __pic__ %d\n", l, l);
		bstr(&pre, b);
		if (pie) {
			snprintf(b, sizeof(b), "#define __PIE__ %d\n#define __pie__ %d\n", l, l);
			bstr(&pre, b);
		}
	}
	if (freestanding) bstr(&pre, "#undef __STDC_HOSTED__\n#define __STDC_HOSTED__ 0\n");
	if (cfi) bstr(&pre, "#define __GCC_HAVE_DWARF2_CFI_ASM 1\n");
	if (strict_align) bstr(&pre, "#undef __ARM_FEATURE_UNALIGNED\n");
	if (tiny) bstr(&pre, "#undef __AARCH64_CMODEL_SMALL__\n#define __AARCH64_CMODEL_TINY__ 1\n");
	if (cmdl.n) bput(&pre, cmdl.p, cmdl.n);
	if (incl.n) bput(&pre, incl.p, incl.n);
	bput(&pre, "", 1);
	po.predef = pre.p;
	po.main = main_file;
	po.unsigned_char = !darwin;
	po.asm_mode = asm_mode;
	if (pmap) {
		char *eq = strchr(pmap, '=');

		if (eq) {
			*eq = 0;
			po.prefix_from = pmap;
			po.prefix_to = eq + 1;
		}
	}

	if (getenv("VPP_WARM")) {	/* passes to fill the token cache (and to profile), then time one */
		for (int r = atoi(getenv("VPP_WARM")); r > 0; r--) {
			p = pp_open(&po);
			do
				pp_next(p, &t);
			while (t.kind != T_EOF);
		}
	}
	t0 = now();
	p = pp_open(&po);
	for (;;) {
		size_t l;
		const char *s;

		pp_next(p, &t);
		if (t.kind == T_EOF)
			break;
		if (t.kind == P_DIRECTIVE) {
			if (col)
				bput(&o, "\n", 1);
			bput(&o, "#", 1);
			bput(&o, t.s, t.len);
			bput(&o, "\n", 1);
			col = 0;
			continue;
		}
		if ((t.flags & TF_SOL) && col) {
			bput(&o, "\n", 1);
			col = 0;
		}
		if (col)
			bput(&o, " ", 1);
		s = pp_spell(&t, sb, &l);
		bput(&o, s, l);
		col++;
	}
	if (col)
		bput(&o, "\n", 1);
	if (getenv("VPP_TIME"))
		fprintf(stderr, "vpp: %.1f ms, %zu bytes out\n", (now() - t0) * 1e3, o.n);
	if (pp_errors(p))
		return 1;
	if (cmp)
		return compare(o.p, o.n, cmp);
	if (out) {
		FILE *f = fopen(out, "w");

		if (!f) {
			perror(out);
			return 1;
		}
		fwrite(o.p, 1, o.n, f);
		fclose(f);
	} else {
		fwrite(o.p, 1, o.n, stdout);
	}
	return 0;
}
