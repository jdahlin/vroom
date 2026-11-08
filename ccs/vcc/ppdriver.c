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
 * With -c the TU is compiled (C: cc.c + emit.c, .S: as.c) into an ELF
 * object, written with one write(); the dependency file is written as for -E.
 * Each thread keeps one assembler, one ElfObj and one output buffer.
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
#include "as.h"
#include "elf.h"
#include "ppdata-predef.inc"
#include "arena.h"

typedef struct buf {
	char *p;
	size_t n, cap;
	vmreg *r;	/* the first 16 MB come from this per-thread region (bfree puts it back) */
} buf;

#define BUF_REG (1u << 24)
static vmpool bufpool = VMPOOL_INIT(4);
static _Thread_local vmreg *bufr;	/* this job's (ppd_run) */

static void bput(buf *b, const char *s, size_t n)
{
	if (b->n + n + 256 > b->cap) {
		size_t nc = b->cap ? b->cap * 2 : BUF_REG;
		char *np;

		while (nc < b->n + n + 256)
			nc *= 2;
		if (!b->cap && b->r && nc == BUF_REG) {
			b->p = vmreg_get(b->r, BUF_REG, "ppdriver.buf");
			b->cap = BUF_REG;
			goto put;
		}
		np = mmap(NULL, nc, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (np == MAP_FAILED)
			abort();
		if (b->p) {
			memcpy(np, b->p, b->n);
			if (b->r)
				vmreg_put(b->r, b->n);
			else
				munmap(b->p, b->cap);
		}
		b->r = NULL;
		b->p = np;
		b->cap = nc;
	}
put:
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
		vlex_no_intern = 1;	/* the copies are freed: nothing may point into them */
		A = lexbuf(ours, on);
		B = lexbuf(m, (size_t)st.st_size);
		vlex_no_intern = 0;
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
	if (b->p && b->r)
		vmreg_put(b->r, b->n + 256);
	else if (b->p)
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
	buf d = { .r = &bufr[3] };
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

/* phase times over all -c jobs (ns): front end + codegen, assembler, finish + write */
_Atomic unsigned long long ppd_ns_cc, ppd_ns_emit, ppd_ns_obj, ppd_bytes_obj;
_Thread_local vperf ppd_ph[PPD_NPH];

static unsigned long long nsnow(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

/*
 * a job's object state: assembler, ELF writer, serialised output; from a
 * process-wide pool, so it outlives threads (vpp --batch starts some per pass)
 */
static _Thread_local As *tl_as;
static _Thread_local ElfObj *tl_obj;
static _Thread_local uint8_t *tl_objbuf;
#define OBJBUF_CAP ((size_t)1 << 32)

static struct objst { As *as; ElfObj *obj; uint8_t *buf; struct objst *next; } *obj_free;
static _Thread_local struct objst *tl_os;
static pthread_mutex_t obj_mu = PTHREAD_MUTEX_INITIALIZER;

static void obj_state(void)
{
	struct objst *s;

	if (tl_os)
		return;
	pthread_mutex_lock(&obj_mu);
	s = obj_free;
	if (s)
		obj_free = s->next;
	pthread_mutex_unlock(&obj_mu);
	if (!s) {
		s = calloc(1, sizeof(*s));
		s->as = as_new();
		s->obj = elf_new();
		s->buf = mmap(NULL, OBJBUF_CAP, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
		if (s->buf == MAP_FAILED)
			abort();
	}
	tl_os = s;
	tl_as = s->as, tl_obj = s->obj, tl_objbuf = s->buf;
}

static void obj_release(void)
{
	struct objst *s = tl_os;

	if (!s)
		return;
	tl_os = NULL;
	tl_as = NULL, tl_obj = NULL, tl_objbuf = NULL;
	pthread_mutex_lock(&obj_mu);
	s->next = obj_free;
	obj_free = s;
	pthread_mutex_unlock(&obj_mu);
}

/* lay out tl_obj and write it to out */
static int write_obj(const char *cwd, const char *out, int outfd, int errfd, int macho)
{
	unsigned long long t0 = nsnow();
	vperf v0 = vperf_now();
	size_t n = macho ? macho_finish(tl_obj) : elf_finish(tl_obj);
	int r;

	if (!n || tl_obj->has_err) {
		dprintf(errfd, "vcc: error: %s\n", tl_obj->err[0] ? tl_obj->err : "cannot lay out the object");
		return 1;
	}
	if (n > OBJBUF_CAP) {
		dprintf(errfd, "vcc: error: object too large\n");
		return 1;
	}
	if (macho)
		macho_emit(tl_obj, tl_objbuf);
	else
		elf_emit(tl_obj, tl_objbuf);
	r = write_out(cwd, out, outfd, (const char *)tl_objbuf, n, errfd);
	ppd_ns_obj += nsnow() - t0;
	ppd_ph_add(PPD_PH_OBJ, v0);
	ppd_bytes_obj += n;
	return r;
}

/* the default object name: the source's basename with .o */
static const char *obj_name(const char *main_file, char *buf, size_t cap)
{
	const char *b = strrchr(main_file, '/') ? strrchr(main_file, '/') + 1 : main_file;
	const char *dot = strrchr(b, '.');

	snprintf(buf, cap, "%.*s.o", (int)(dot ? dot - b : (long)strlen(b)), b);
	return buf;
}

int ppd_run(int argc, char **argv, const ppd_env *env)
{
	const char *dirs_q[256], *dirs_i[1024], *dirs_s[256], *dirs_a[256], *all[2048];
	int nq = 0, ni = 0, ns = 0, na = 0, n = 0;
	const char *cmp = NULL, *out = NULL, *main_file = NULL, *depfile = NULL, *deptarget = NULL;
	int errfd = env->errfd, status = 0, md = 0, dcolor = 0, nocaret = 0, elimit = 0, nwargs = 0;
	const char **wargs = NULL;
	char pmapbuf[4096];
	jmp_buf jb;
	vmreg *br = bufr = vmpool_take(&bufpool);
	buf pre = { .r = &br[0] }, cmdl = { .r = &br[1] }, incl = { .r = &br[2] }, o = { 0 };
	int darwin = 1, nostdinc = 0, asm_mode = 0, undef = 0;
	int opt = 2, os = 0, pie = 0, pic = 0, freestanding = 0, nobuiltin = 0, instr = 0, cfi = 0, strict_align = 0, tiny = 0, gregs = 0;
	pp_opts po = { 0 };
	char *pmap = NULL;
	double t0;
	pp *p;
	int syntax = 0, layout = 0, ms_anon = 0, short_wchar = 0, tokhash = 0, emit = 0, objmode = 0;
	int std = 0, strict = 0, wimpl_int = 0, wimpl_fn = 0, gnu89_inline = -1;
	char objbuf[1024];

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define ARG(flag) (!strcmp(a, flag) ? (i + 1 < argc ? argv[++i] : "") : !strncmp(a, flag, strlen(flag)) ? a + strlen(flag) : NULL)
		const char *v;

		if (!strcmp(a, "--cmp") && i + 1 < argc) { cmp = argv[++i]; continue; }
		if (!strcmp(a, "-fsyntax-only")) { syntax = 1; continue; }
		if (!strcmp(a, "-S")) { syntax = 1; emit = 1; continue; }
		if (!strcmp(a, "-c")) { objmode = 1; continue; }
		if (!strcmp(a, "-fms-anonymous-structs") || !strcmp(a, "-fms-extensions")) { ms_anon = 1; continue; }
		if (!strcmp(a, "-fshort-wchar")) { short_wchar = 1; continue; }
		if (!strcmp(a, "-fcolor-diagnostics") || !strcmp(a, "-fdiagnostics-color") || !strcmp(a, "-fdiagnostics-color=always")) { dcolor = 1; continue; }
		if (!strcmp(a, "-fno-color-diagnostics") || !strcmp(a, "-fno-diagnostics-color") || !strcmp(a, "-fdiagnostics-color=never")) { dcolor = -1; continue; }
		if (!strcmp(a, "-fdiagnostics-color=auto")) { dcolor = 0; continue; }
		if (!strcmp(a, "-fno-caret-diagnostics")) { nocaret = 1; continue; }
		if (!strcmp(a, "-fcaret-diagnostics")) { nocaret = 0; continue; }
		if (!strncmp(a, "-ferror-limit=", 14)) { elimit = atoi(a + 14); elimit = elimit ? elimit : -1; continue; }
		if (!strncmp(a, "-std=", 5)) {
			/* clang's spellings: c89 c90 iso9899:1990 gnu89 gnu90 c99 gnu99 c11 gnu11 c17 c18 gnu17 c23 c2x gnu23 ... */
			static const struct { const char *s; int v; } stds[] = {
				{ "89", 89 }, { "90", 89 }, { "1990", 89 }, { "199409", 89 }, { "99", 99 }, { "9x", 99 },
				{ "1999", 99 }, { "11", 11 }, { "1x", 11 }, { "2011", 11 }, { "17", 17 }, { "18", 17 },
				{ "2017", 17 }, { "2018", 17 }, { "23", 23 }, { "2x", 23 }, { "2y", 23 }, { "2024", 23 },
			};
			const char *v = a + 5;

			strict = strncmp(v, "gnu", 3) != 0;
			v += strncmp(v, "gnu", 3) == 0 ? 3 : v[0] == 'c' ? 1 : strncmp(v, "iso9899:", 8) == 0 ? 8 : 0;
			for (size_t k = 0; k < sizeof(stds) / sizeof(stds[0]); k++)
				if (!strcmp(v, stds[k].s))
					std = stds[k].v;
			continue;
		}
		if (!strcmp(a, "-ansi")) { std = 89; strict = 1; continue; }
		if (!strcmp(a, "-fgnu89-inline")) { gnu89_inline = 1; continue; }
		if (!strcmp(a, "-fno-gnu89-inline")) { gnu89_inline = 0; continue; }
		if ((a[1] == 'W' && a[2] && a[3] != ',') || !strcmp(a, "-w")) {
			/* warning options, in order, for pp_wstate (some are also handled below) */
			if (!wargs)
				wargs = calloc((size_t)argc + 1, sizeof(*wargs));
			wargs[nwargs++] = a;
		}
		if (!strcmp(a, "-Wno-implicit-int") || !strcmp(a, "-Wno-error=implicit-int")) { wimpl_int = 1; continue; }
		if (!strcmp(a, "-Wno-implicit-function-declaration") || !strcmp(a, "-Wno-error=implicit-function-declaration")) {
			wimpl_fn = 1;
			continue;
		}
		if (!strcmp(a, "--vcc-layout-checks")) { syntax = 1; layout = 1; continue; }
		if (!strcmp(a, "--vcc-tokhash")) { tokhash = 1; continue; }
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
		else if (!strcmp(a, "-fno-builtin")) nobuiltin = 1;
		else if (!strcmp(a, "-finstrument-functions")) instr = 1;
		else if (!strcmp(a, "-fasynchronous-unwind-tables")) cfi = 1;
		else if (!strcmp(a, "-fno-asynchronous-unwind-tables")) cfi = 0;
		else if (!strcmp(a, "-mstrict-align")) strict_align = 1;
		else if (!strcmp(a, "-mgeneral-regs-only")) gregs = 1;
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
		status = 2;
		goto out;
	}
	if (objmode) {
		obj_state();
		if (!out)
			out = obj_name(main_file, objbuf, sizeof(objbuf));
		if (!asm_mode) {
			syntax = 1;
			emit = 2;
		}
	}
	/* search path: -iquote, -I, -isystem, -idirafter; exact duplicates dropped */
	for (int k = 0; k < nq; k++) all[n++] = dirs_q[k];
	po.nquote = n;
#define ADD(d) do { int dup_ = 0; for (int j = po.nquote; j < n; j++) dup_ |= !strcmp(all[j], d); if (!dup_) all[n++] = d; } while (0)
	for (int k = 0; k < ni; k++) ADD(dirs_i[k]);
	po.nsys = n;
	for (int k = 0; k < ns; k++) {
		/* vcc's own headers (arm_neon.h) go before the clang resource directory */
		if (strstr(dirs_s[k], "/lib/clang/") && strlen(dirs_s[k]) > 8 &&
		    !strcmp(dirs_s[k] + strlen(dirs_s[k]) - 8, "/include"))
			ADD(VCC_INCDIR);
		ADD(dirs_s[k]);
	}
	/* Darwin: vcc's own <stdatomic.h>, <arm_neon.h>, <arm_acle.h> ahead of the SDK's */
	if (darwin && !nostdinc)
		ADD(VCC_INCDIR);
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
	else if (!undef && std) {
		/* the tables are for -std=gnu11 (the kernel's); other standards as clang predefines them */
		static const char *const ver[] = { [89] = NULL, [99] = "199901L", [11] = "201112L", [17] = "201710L", [23] = "202311L" };

		bstr(&pre, "#undef __STDC_VERSION__\n");
		if (ver[std]) {
			bstr(&pre, "#define __STDC_VERSION__ ");
			bstr(&pre, ver[std]);
			bstr(&pre, "\n");
		} else {
			bstr(&pre, "#undef __GNUC_STDC_INLINE__\n#define __GNUC_GNU_INLINE__ 1\n");
		}
		if (strict)
			bstr(&pre, "#define __STRICT_ANSI__ 1\n#undef linux\n#undef unix\n");
	} else if (!undef && !asm_mode) {
		bstr(&pre, "#undef __STDC_VERSION__\n#define __STDC_VERSION__ 201710L\n");
	}
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
	if (gnu89_inline < 0)
		gnu89_inline = std == 89;
	else if (!undef && !asm_mode)
		bstr(&pre, gnu89_inline ? "#undef __GNUC_STDC_INLINE__\n#undef __GNUC_GNU_INLINE__\n#define __GNUC_GNU_INLINE__ 1\n"
					: "#undef __GNUC_GNU_INLINE__\n#undef __GNUC_STDC_INLINE__\n#define __GNUC_STDC_INLINE__ 1\n");
	if (freestanding) bstr(&pre, "#undef __STDC_HOSTED__\n#define __STDC_HOSTED__ 0\n");
	if (!darwin && !short_wchar && !asm_mode && !undef)	/* the Linux table is for the kernel's -fshort-wchar */
		bstr(&pre, "#undef __ARM_SIZEOF_WCHAR_T\n#define __ARM_SIZEOF_WCHAR_T 4\n"
			   "#undef __SIZEOF_WCHAR_T__\n#define __SIZEOF_WCHAR_T__ 4\n"
			   "#undef __WCHAR_MAX__\n#define __WCHAR_MAX__ 4294967295U\n"
			   "#undef __WCHAR_MIN__\n#define __WCHAR_MIN__ 0U\n"
			   "#undef __WCHAR_TYPE__\n#define __WCHAR_TYPE__ unsigned int\n"
			   "#undef __WCHAR_WIDTH__\n#define __WCHAR_WIDTH__ 32\n"
			   "#undef __clang_wide_literal_encoding__\n#define __clang_wide_literal_encoding__ \"UTF-32\"\n");
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
	po.diag_color = dcolor;
	po.diag_nocaret = nocaret;
	po.error_limit = elimit;
	po.wargs = wargs, po.nwargs = nwargs;
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
	if (tokhash) {
		/* a hash of what pp_next hands the parser: kind, SOL/SPACE, spelling, id, file:line:col, sys */
		uint64_t h = 1469598103934665603ull;
		unsigned long n = 0;
		char tmp[1 << 16], line[64];
		const char *fn[4096];
		size_t nf;
		ptok t;
		const int count_only = getenv("VPP_TOKCOUNT") != NULL;

#define HB(ptr, len) do { const unsigned char *q_ = (const unsigned char *)(ptr); \
		for (size_t k_ = 0; k_ < (size_t)(len); k_++) h = (h ^ q_[k_]) * 1099511628211ull; } while (0)
		for (;;) {
			const char *sp;
			size_t sl;
			unsigned char kf[2];

			pp_next(p, &t);
			if (t.kind == T_EOF)
				break;
			n++;
			if (count_only)
				continue;
			kf[0] = t.kind, kf[1] = t.flags & (TF_SOL | TF_SPACE);
			HB(kf, 2);
			HB(&t.loc, 4);	/* the same in every run: file bases are assigned in order */
			sp = t.len < sizeof(tmp) ? pp_spell(&t, tmp, &sl) : (sl = 0, "");
			HB(sp, sl);
			if (t.kind == T_IDENT)
				HB(vintern_ents[t.id].s, vintern_ents[t.id].len);
		}
		nf = pp_files(p, fn, 4096);
		for (size_t i = 0; i < nf; i++)
			HB(fn[i], strlen(fn[i]) + 1);
#undef HB
		snprintf(line, sizeof(line), "%016llx %lu\n", (unsigned long long)h, n);
		status = pp_errors(p) ? 1 : write_out(env->cwd, out, env->outfd, line, strlen(line), errfd);
		goto done;
	}
	if (syntax) {
		cc_opts co = { .target = po.target, .unsigned_char = po.unsigned_char, .dump_layouts = layout, .ms_anon = ms_anon, .short_wchar = short_wchar,
				.std = std, .implicit_int = std == 89 || wimpl_int, .implicit_fn = std == 89 || wimpl_fn,
				.gnu89_inline = gnu89_inline, .no_builtin = freestanding || nobuiltin, .instrument = instr,
				.general_regs_only = gregs,
			       .outfd = env->outfd };
		int fd = -1;

		co.emit = emit;
		co.nthreads = getenv("VCC_THREADS") ? atoi(getenv("VCC_THREADS")) : 1;
		co.obj = tl_obj;
		co.as = tl_as;
		if ((layout || emit == 1) && out && strcmp(out, "-")) {
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
		{
			unsigned long long t1 = nsnow();
			vperf v1 = vperf_now();

			status = cc_run(p, &co) || pp_errors(p) ? 1 : 0;
			ppd_ns_cc += nsnow() - t1;
			ppd_ph_add(PPD_PH_CC, v1);
		}
		if (fd >= 0)
			close(fd);
		if (!status && emit == 2)
			status = write_obj(env->cwd, out, env->outfd, errfd, darwin);
		if (!status && emit == 2 && depfile)
			status = write_deps(p, env->cwd, depfile, deptarget ? deptarget : out, errfd);
		if (getenv("VPP_TIME"))
			dprintf(errfd, "vcc: %.1f ms\n", (now() - t0) * 1e3);
		goto done;
	}
	{
		const char *s;
		size_t l;

		pp_text(p, &s, &l);
		o.p = (char *)s;
		o.n = l;
	}
	if (getenv("VPP_TIME")) {
		extern unsigned long pp_stat_try, pp_stat_skip, vdir_probes;

		dprintf(errfd, "vpp: %.1f ms, %zu bytes out; include candidates %lu, %lu ruled out by the directory cache (%lu dirs probed)\n",
			(now() - t0) * 1e3, o.n, pp_stat_try, pp_stat_skip, vdir_probes);
	}
	if (pp_errors(p)) {
		status = 1;
	} else if (cmp) {
		status = ppd_compare(o.p, o.n, cmp);
	} else if (objmode) {
		const char *msg;

		as_set_dir(tl_as, env->cwd);
		if (as_run(tl_as, tl_obj, o.p, o.n, main_file, &msg)) {
			dprintf(errfd, "vcc: error: %s\n", msg);
			status = 1;
		} else {
			status = write_obj(env->cwd, out, env->outfd, errfd, 0);
		}
		if (!status && depfile)
			status = write_deps(p, env->cwd, depfile, deptarget ? deptarget : out, errfd);
	} else {
		status = write_out(env->cwd, out, env->outfd, o.p, o.n, errfd);
		if (!status && depfile)
			status = write_deps(p, env->cwd, depfile, deptarget ? deptarget : out ? out : "-", errfd);
	}
done:
	pp_diag_summary(p);
	pp_close(p);	/* frees the text in o */
out:
	bfree(&pre);
	bfree(&cmdl);
	bfree(&incl);
	vmpool_give(&bufpool, br);
	bufr = NULL;
	obj_release();
	return status;
}
