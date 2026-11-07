/*
 * ppdriver: one preprocessing job (-E) from a clang command line.  Used by
 * vpp (parity tests) and by vccd (kbuild's -E commands: linker scripts,
 * device trees).  Thread safe; no process state (cwd, fds) is touched.
 *
 * Takes the options kbuild passes (-I, -iquote, -isystem, -idirafter,
 * -include, -D, -U, -undef, -nostdinc, -x, --target, -O, the -f/-m flags
 * that change predefined macros, -fmacro-prefix-map, -o, -Wp,-MMD,FILE /
 * -MD -MF -MT) and prints what clang -E -P prints.  With --cmp REF.i the
 * output is compared with REF.i token by token instead (vcc's lexer: kind
 * and spelling); the first difference is shown, status 1.
 *
 *   VPP_TIME=1   report the preprocessing time on the error fd
 */
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "intern.h"
#include "pp.h"
#include "cc.h"
#include "ppdriver.h"
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
	size_t sn = vlex_scratch_size(n) + 4096;
	void *scr = mmap(NULL, sn, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

	memcpy(src, s, n);
	f.src = src;
	f.size = (uint32_t)n;
	f.ntok = vlex(src, n, tok, scr);
	f.tok = tok;
	munmap(scr, sn);
	return f;
}

static void lexfree(vlex_file *f)
{
	munmap((void *)f->src, f->size + 4096);
	munmap((void *)f->tok, (f->size + 2) * sizeof(vtok));
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

static int cmp_toks(const vlex_file *pa, const vlex_file *pb, char *ba, char *bb);

int ppd_compare(const char *ours, size_t on, const char *refpath)
{
	vintern_init();
	int fd = open(refpath, O_RDONLY);
	struct stat st;
	vlex_file A, B;
	static _Thread_local char *ba, *bb;
	int r;

	if (fd < 0 || fstat(fd, &st)) {
		perror(refpath);
		return 2;
	}
	if (!ba) {
		ba = malloc(1 << 20);
		bb = malloc(1 << 20);
	}
	{
		char *m = mmap(NULL, (size_t)st.st_size + 1, PROT_READ, MAP_PRIVATE, fd, 0);

		close(fd);
		A = lexbuf(ours, on);
		B = lexbuf(m, (size_t)st.st_size);
		munmap(m, (size_t)st.st_size + 1);
	}
	r = cmp_toks(&A, &B, ba, bb);
	lexfree(&A);
	lexfree(&B);
	return r;
}

static int cmp_toks(const vlex_file *pa, const vlex_file *pb, char *ba, char *bb)
{
	const vlex_file A = *pa, B = *pb;

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


static void bfree(buf *b)
{
	if (b->p)
		munmap(b->p, b->cap);
	b->p = NULL;
	b->n = b->cap = 0;
}

/* path relative to cwd (if set) */
static const char *at(const char *cwd, const char *path, char *buf, size_t cap)
{
	if (!cwd || path[0] == '/')
		return path;
	snprintf(buf, cap, "%s/%s", cwd, path);
	return buf;
}

static int write_all(int fd, const char *p, size_t n)
{
	while (n) {
		ssize_t w = write(fd, p, n);

		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return -1;
		p += w;
		n -= (size_t)w;
	}
	return 0;
}

static int write_file(const char *cwd, const char *path, const char *s, size_t n, int errfd)
{
	char b[8192];
	const char *f = at(cwd, path, b, sizeof(b));
	int fd = open(f, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);

	if (fd < 0 || write_all(fd, s, n)) {
		dprintf(errfd, "vcc: error: cannot write %s: %s\n", path, strerror(errno));
		if (fd >= 0)
			close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

static int write_out(const char *cwd, const char *out, int outfd, const char *s, size_t n, int errfd)
{
	if (out && strcmp(out, "-"))
		return write_file(cwd, out, s, n, errfd);
	return write_all(outfd, s, n) ? 1 : 0;
}

/* make-style dependency file, as clang -MD writes it */
static int write_deps(const pp *p, const char *cwd, const char *depfile, const char *target, int errfd)
{
	static _Thread_local const char *names[1 << 16];
	size_t n = pp_files(p, names, sizeof(names) / sizeof(names[0]));
	buf d = { 0 };
	int r;

	bstr(&d, target);
	bstr(&d, ":");
	for (size_t i = 0; i < n; i++) {
		const char *f = names[i];

		while (f[0] == '.' && f[1] == '/')	/* clang drops "./" */
			f += 2;
		bstr(&d, " \\\n  ");
		bstr(&d, f);
	}
	bstr(&d, "\n");
	r = write_file(cwd, depfile, d.p, d.n, errfd);
	bfree(&d);
	return r;
}

int ppd_run(int argc, char **argv, const ppd_env *env)
{
	const char *dirs_q[256], *dirs_i[1024], *dirs_s[256], *dirs_a[256], *all[2048];
	int nq = 0, ni = 0, ns = 0, na = 0, n = 0;
	const char *cmp = NULL, *out = NULL, *main_file = NULL, *depfile = NULL, *deptarget = NULL;
	int errfd = env->errfd, status = 0, md = 0;
	char pmapbuf[4096];
	jmp_buf jb;
	buf pre = { 0 }, cmdl = { 0 }, incl = { 0 }, o = { 0 };
	int darwin = 1, nostdinc = 0, asm_mode = 0, undef = 0;
	int opt = 2, os = 0, pie = 0, pic = 0, freestanding = 0, cfi = 0, strict_align = 0, tiny = 0;
	pp_opts po = { 0 };
	char *pmap = NULL;
	double t0;
	pp *p;
	ptok t;
	int col = 0, syntax = 0, layout = 0, ms_anon = 0, short_wchar = 0;
	char sb[1 << 16];

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define ARG(flag) (!strcmp(a, flag) ? (i + 1 < argc ? argv[++i] : "") : !strncmp(a, flag, strlen(flag)) ? a + strlen(flag) : NULL)
		const char *v;

		if (!strcmp(a, "--cmp") && i + 1 < argc) { cmp = argv[++i]; continue; }
		if (!strcmp(a, "-fsyntax-only")) { syntax = 1; continue; }
		if (!strcmp(a, "-fms-anonymous-structs") || !strcmp(a, "-fms-extensions")) { ms_anon = 1; continue; }
		if (!strcmp(a, "-fshort-wchar")) { short_wchar = 1; continue; }
		if (!strcmp(a, "--vcc-layout-checks")) { syntax = 1; layout = 1; continue; }
		if (!strcmp(a, "-o") && i + 1 < argc) { out = argv[++i]; continue; }
		if (!strncmp(a, "-Wp,-MMD,", 9) || !strncmp(a, "-Wp,-MD,", 8)) { depfile = strchr(a + 4, ',') + 1; continue; }
		if (!strcmp(a, "-MD") || !strcmp(a, "-MMD")) { md = 1; continue; }
		if (!strcmp(a, "-MF") && i + 1 < argc) { depfile = argv[++i]; continue; }
		if (!strcmp(a, "-MT") && i + 1 < argc) { deptarget = argv[++i]; continue; }
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
			snprintf(pmapbuf, sizeof(pmapbuf), "%s", a + 19);
			pmap = pmapbuf;
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
	(void)md;
	if (!main_file) {
		dprintf(errfd, "vcc: error: no input file\n");
		return 2;
	}
	/* search path: -iquote, -I, -isystem, -idirafter; exact duplicates dropped */
	for (int k = 0; k < nq; k++) all[n++] = dirs_q[k];
	po.nquote = n;
#define ADD(d) do { int dup_ = 0; for (int j = po.nquote; j < n; j++) dup_ |= !strcmp(all[j], d); if (!dup_) all[n++] = d; } while (0)
	for (int k = 0; k < ni; k++) ADD(dirs_i[k]);
	po.nsys = n;
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
	po.cwd = env->cwd;
	po.errfd = errfd;
	po.asm_mode = asm_mode;
	if (pmap) {
		char *eq = strchr(pmap, '=');

		if (eq) {
			*eq = 0;
			po.prefix_from = pmap;
			po.prefix_to = eq + 1;
		}
	}

	t0 = now();
	p = pp_open(&po);
	if (!p) {
		status = 1;
		goto out;
	}
	pp_set_fatal(p, &jb);
	if (setjmp(jb)) {
		status = 1;
		goto done;
	}
	if (syntax) {
		cc_opts co = { .target = po.target, .unsigned_char = po.unsigned_char, .dump_layouts = layout, .ms_anon = ms_anon, .short_wchar = short_wchar,
			       .outfd = env->outfd };
		int fd = -1;

		if (layout && out) {
			char path[4096];

			if (out[0] != '/' && env->cwd)
				snprintf(path, sizeof(path), "%s/%s", env->cwd, out);
			else
				snprintf(path, sizeof(path), "%s", out);
			fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd < 0) {
				dprintf(errfd, "vcc: error: cannot open %s\n", out);
				status = 1;
				goto done;
			}
			co.outfd = fd;
		}
		status = cc_run(p, &co) || pp_errors(p) ? 1 : 0;
		if (fd >= 0)
			close(fd);
		if (getenv("VPP_TIME"))
			dprintf(errfd, "vcc: %.1f ms\n", (now() - t0) * 1e3);
		goto done;
	}
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
	if (getenv("VPP_TIME")) {
		extern unsigned long pp_stat_try, pp_stat_skip, vdir_probes;

		dprintf(errfd, "vpp: %.1f ms, %zu bytes out; include candidates %lu, %lu ruled out by the directory cache (%lu dirs probed)\n",
			(now() - t0) * 1e3, o.n, pp_stat_try, pp_stat_skip, vdir_probes);
	}
	if (pp_errors(p)) {
		status = 1;
	} else if (cmp) {
		status = ppd_compare(o.p, o.n, cmp);
	} else {
		status = write_out(env->cwd, out, env->outfd, o.p, o.n, errfd);
		if (!status && depfile)
			status = write_deps(p, env->cwd, depfile, deptarget ? deptarget : out ? out : "-", errfd);
	}
done:
	pp_close(p);
out:
	bfree(&pre);
	bfree(&cmdl);
	bfree(&incl);
	bfree(&o);
	return status;
}
