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
 * and spelling); the first difference is shown, status 1.  The reference's
 * token-stream hash is kept in REF.i.th (keyed by its size and mtime): when
 * ours hashes the same, the reference is not read again.
 *
 * With -c the TU is compiled (C: cc/ + emit/, .S: as.c) into an ELF
 * object, written with one write(); the dependency file is written as for -E.
 * Each thread keeps one assembler, one ElfObj and one output buffer.
 *
 *   VPP_TIME=1   report the preprocessing time on the error fd
 */
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <poll.h>
#include <sys/socket.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../lib/intern.h"
#include "../pp/pp.h"
#include "../cc/cc.h"
#include "ppdriver.h"
#include "../as/as.h"
#include "../obj/obj.h"
#include "../pp/ppdata-predef.inc"
#include "../lib/arena.h"
#include "../lib/ldhash.h"

#ifdef __APPLE__
#define ST_MTIME_NS(st) ((unsigned long long)(st)->st_mtimespec.tv_sec * 1000000000ull + (unsigned long long)(st)->st_mtimespec.tv_nsec)
#else
#define ST_MTIME_NS(st) ((unsigned long long)(st)->st_mtim.tv_sec * 1000000000ull + (unsigned long long)(st)->st_mtim.tv_nsec)
#endif

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

/* XXH3 of a token stream: per token the kind, the clean spelling's length and
 * the spelling (what cmp_toks compares) */
static uint64_t tokstream_hash(const vlex_file *f)
{
	size_t cap = (size_t)f->size + (size_t)f->ntok * 5 + 64, n = 0;
	unsigned char *b = mmap(NULL, cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	uint64_t h;

	for (uint32_t i = 0; i < f->ntok; i++) {
		const vtok *t = &f->tok[i];
		size_t l;

		if (n + 5 + t->len + 64 > cap)	/* a clean spelling is never longer than the raw one */
			break;
		b[n] = (unsigned char)t->kind;
		l = clean(f, t, (char *)b + n + 5);
		memcpy(b + n + 1, &(uint32_t){ (uint32_t)l }, 4);
		n += 5 + l;
	}
	h = ld_xxh3(b, n) ^ f->ntok;
	munmap(b, cap);
	return h;
}

/* REF.i.th: "size mtime_ns ntok hash" of REF.i's token stream, so a run that
 * matches lexes only our side; written on the first compare of a REF.i */
static int refhash_read(const char *refpath, const struct stat *st, uint64_t *h)
{
	char path[4096], b[128];
	unsigned long long sz, mt, nt, hv;
	int fd;
	ssize_t r;

	snprintf(path, sizeof(path), "%s.th", refpath);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	r = read(fd, b, sizeof(b) - 1);
	close(fd);
	if (r <= 0)
		return 0;
	b[r] = 0;
	if (sscanf(b, "%llu %llu %llu %llx", &sz, &mt, &nt, &hv) != 4 || sz != (unsigned long long)st->st_size ||
	    mt != ST_MTIME_NS(st))
		return 0;
	*h = hv;
	return 1;
}

static void refhash_write(const char *refpath, const struct stat *st, uint32_t ntok, uint64_t h)
{
	char path[4096], tmp[4200], b[128];
	int fd, n;

	snprintf(path, sizeof(path), "%s.th", refpath);
	snprintf(tmp, sizeof(tmp), "%s.%d.%p", path, (int)getpid(), (void *)&fd);
	n = snprintf(b, sizeof(b), "%llu %llu %u %016llx\n", (unsigned long long)st->st_size,
		     ST_MTIME_NS(st), ntok, (unsigned long long)h);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
	if (fd < 0)
		return;
	if (write(fd, b, (size_t)n) == n && !close(fd))
		rename(tmp, path);
	else
		unlink(tmp);
}

int ppd_compare(const char *ours, size_t on, const char *refpath)
{
	vintern_init();
	int fd = open(refpath, O_RDONLY);
	struct stat st;
	vlex_file A, B;
	static _Thread_local char *ba, *bb;
	uint64_t rh;
	int r, cached;

	if (fd < 0 || fstat(fd, &st)) {
		perror(refpath);
		return 2;
	}
	if (!ba) {
		ba = malloc(1 << 20);
		bb = malloc(1 << 20);
	}
	vlex_no_intern = 1;	/* the copies are freed: nothing may point into them */
	A = lexbuf(ours, on);
	/* VPP_CMP_NOCACHE=1: always lex and compare the reference */
	cached = !getenv("VPP_CMP_NOCACHE") && refhash_read(refpath, &st, &rh);
	if (cached && tokstream_hash(&A) == rh) {
		vlex_no_intern = 0;
		close(fd);
		printf("same: %u tokens\n", A.ntok);
		lexfree(&A);
		return 0;
	}
	{
		char *m = mmap(NULL, (size_t)st.st_size + 1, PROT_READ, MAP_PRIVATE, fd, 0);

		close(fd);
		B = lexbuf(m, (size_t)st.st_size);
		vlex_no_intern = 0;
		munmap(m, (size_t)st.st_size + 1);
	}
	if (!cached)
		refhash_write(refpath, &st, B.ntok, tokstream_hash(&B));
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

/* outputs go to a temporary next to the target, renamed over it when complete:
   an interrupted or failed job leaves the old file or none, never a torn one */
_Thread_local int ppd_umask = -1;
_Thread_local int ppd_conn = -1;

/* the client closed its end (it was killed, or make was interrupted): the
 * request is all it sends, so anything readable now is the EOF */
static bool client_gone(void)
{
	struct pollfd pf = { ppd_conn, POLLIN, 0 };
	char c;

	return ppd_conn >= 0 && poll(&pf, 1, 0) == 1 && recv(ppd_conn, &c, 1, MSG_PEEK | MSG_DONTWAIT) == 0;
}

/* a new output's mode: the client's umask under vccd */
static void out_mode(int fd)
{
	if (ppd_umask >= 0)
		fchmod(fd, 0666 & ~(mode_t)ppd_umask);
}

static bool tmp_name(char *t, size_t cap, const char *f)
{
	static _Atomic unsigned seq;
	struct stat st;

	/* /dev/null, a fifo, ...: written where it is, a rename would replace it */
	if (!stat(f, &st) && !S_ISREG(st.st_mode)) {
		snprintf(t, cap, "%s", f);
		return false;
	}
	snprintf(t, cap, "%s.vcc%d.%u.tmp", f, (int)getpid(), atomic_fetch_add(&seq, 1));
	return true;
}

static int write_file(const char *cwd, const char *path, const char *s, size_t n, int errfd)
{
	char b[8192], t[8300];
	const char *f = at(cwd, path, b, sizeof(b));
	int fd;

	bool tmp = tmp_name(t, sizeof(t), f);

	if (client_gone()) {
		dprintf(errfd, "vcc: %s not written: the client has gone\n", path);
		return 1;
	}

	fd = open(t, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
	if (fd >= 0 && tmp)
		out_mode(fd);
	if (fd < 0 || write_all(fd, s, n) || close(fd) || (tmp && rename(t, f))) {
		int e = errno;

		dprintf(errfd, "vcc: error: cannot write %s: %s\n", path, strerror(e));
		if (fd >= 0 && tmp)
			unlink(t);
		return 1;
	}
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
	static _Thread_local const char **names;
	static _Thread_local size_t cap;
	size_t n;

	/* grown until every file fits (a full table may have been cut short) */
	while (!names || (n = pp_files(p, names, cap)) == cap) {
		free(names);
		cap = cap ? 2 * cap : 4096;
		names = malloc(cap * sizeof(*names));
	}
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

/* does -march=/-mcpu= name a target with LSE atomics (as clang's
   __ARM_FEATURE_ATOMICS)?  -1: no opinion (keep the previous answer) */
static int arch_lse(const char *v, bool cpu)
{
	const char *plus = strchr(v, '+');
	size_t n = plus ? (size_t)(plus - v) : strlen(v);
	int lse = -1;

	if (!cpu) {
		if (n == 7 && !strncmp(v, "armv8-a", 7))
			lse = 0;
		else if (n >= 5 && (!strncmp(v, "armv8.", 6) || !strncmp(v, "armv9", 5)))
			lse = 1;	/* armv8.1-a and later, armv8-r has none */
		if (n >= 7 && !strncmp(v, "armv8-r", 7))
			lse = 0;
	} else {
		static const char *const old[] = { "generic", "cortex-a35", "cortex-a53", "cortex-a57", "cortex-a72",
						   "cortex-a73", "thunderx", "thunderxt88", "xgene1", "exynos-m3",
						   "cyclone", "apple-a7", "apple-a8", "apple-a9", "apple-a10", "falkor",
						   "kryo", "thunderxt81", "thunderxt83", "cortex-r82", NULL };

		lse = 1;	/* native here is an Apple core */
		for (int i = 0; old[i]; i++)
			if (strlen(old[i]) == n && !strncmp(v, old[i], n))
				lse = 0;
	}
	for (const char *p = plus; p; p = strchr(p + 1, '+')) {
		if (!strncmp(p, "+lse", 4) && (!p[4] || p[4] == '+'))
			lse = 1;
		else if (!strncmp(p, "+nolse", 6) && (!p[6] || p[6] == '+'))
			lse = 0;
	}
	return lse;
}


static void intern_full(void)
{
	if (vjob_jb)
		longjmp(*vjob_jb, 1);
}

/* CPATH-style lists (kept out of ppd_run: its size decides how clang
   compiles the argument loop; vbench pp +3.7 % instructions inline) */
static __attribute__((noinline)) size_t env_ndirs(const char *v)
{
	size_t k = v && *v ? 1 : 0;

	for (; v && *v; v++)
		k += *v == ':';
	return k;
}

/* the elements of v appended to all[n..] (not repeating one from nquote on); an
   empty element is the current directory.  slot: which per-thread copy holds them */
static __attribute__((noinline)) int env_dirs(const char *v, const char **all, int n, int nquote, int slot)
{
	static _Thread_local char *copy[2];
	char *s, *t;

	if (!v || !*v)
		return n;
	free(copy[slot]);
	copy[slot] = s = strdup(v);
	for (; s; s = t) {
		const char *d;
		int dup = 0;

		if ((t = strchr(s, ':')))
			*t++ = 0;
		d = *s ? s : ".";
		for (int j = nquote; j < n; j++)
			dup |= !strcmp(all[j], d);
		if (!dup)
			all[n++] = d;
	}
	return n;
}

/* a variable of the job's environment: the client's under vccd */
static const char *ppd_getenv(const ppd_env *env, const char *name)
{
	size_t l = strlen(name);

	if (!env->envp)
		return getenv(name);
	for (char **e = env->envp; *e; e++)
		if (!strncmp(*e, name, l) && (*e)[l] == '=')
			return *e + l + 1;
	return NULL;
}

/* vcc --explain [ID]: the page of a diagnostic id (ccs/vcc/docs/diag, compiled in), or the
   list of ids; -1 when the command line has no --explain */
static const struct { const char *id, *text; } explain_tab[] = {
#include "explain.inc"
};

static __attribute__((noinline, cold)) int explain(int argc, char **argv, const ppd_env *env)
{
	const char *id = NULL;
	bool any = false;
	int outfd = env->outfd >= 0 ? env->outfd : 1;

	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "--explain")) {
			any = true;
			id = i + 1 < argc ? argv[i + 1] : NULL;
		} else if (!strncmp(argv[i], "--explain=", 10)) {
			any = true;
			id = argv[i] + 10;
		}
	if (!any)
		return -1;
	if (id && id[0] == '[')	/* as -fdiagnostics-show-id prints it: [id] */
		id++;
	for (size_t i = 0; id && i < sizeof(explain_tab) / sizeof(explain_tab[0]); i++) {
		size_t l = strlen(explain_tab[i].id);

		if (!strncmp(id, explain_tab[i].id, l) && (!id[l] || id[l] == ']'))
			return write_all(outfd, explain_tab[i].text, strlen(explain_tab[i].text)) ? 1 : 0;
	}
	if (id)
		dprintf(env->errfd >= 0 ? env->errfd : 2, "vcc: error: no explanation for '%s' (vcc --explain lists the ids)\n", id);
	else
		for (size_t i = 0; i < sizeof(explain_tab) / sizeof(explain_tab[0]); i++)
			dprintf(outfd, "%s\n", explain_tab[i].id);
	return id ? 1 : 0;
}

static __attribute__((noinline)) int ppd_run1(int argc, char **argv, const ppd_env *env);

/* (--explain apart: a call at the top of ppd_run1 costs its token loop registers) */
int ppd_run(int argc, char **argv, const ppd_env *env)
{
	int x = explain(argc, argv, env);

	return x >= 0 ? x : ppd_run1(argc, argv, env);
}

static __attribute__((noinline)) int ppd_run1(int argc, char **argv, const ppd_env *env)
{
	/* each list holds at most argc entries, all of them plus the defaults: one
	   per-thread allocation, grown to the longest command line seen */
	static _Thread_local const char **dirv;
	static _Thread_local size_t dircap;
	const char *cpath = ppd_getenv(env, "CPATH"), *cipath = ppd_getenv(env, "C_INCLUDE_PATH");
	size_t nenvd = env_ndirs(cpath) + env_ndirs(cipath);
	size_t per = (size_t)argc + 64 + nenvd;

	if (5 * per > dircap) {
		free(dirv);
		dircap = 5 * per;
		dirv = malloc(dircap * sizeof(*dirv));
	}
	const char **dirs_q = dirv, **dirs_i = dirv + per, **dirs_s = dirv + 2 * per, **dirs_a = dirv + 3 * per,
		   **all = dirv + 4 * per;
	int nq = 0, ni = 0, ns = 0, na = 0, n = 0;
	const char *cmp = NULL, *out = NULL, *main_file = NULL, *depfile = NULL, *deptarget = NULL;
	int errfd = env->errfd, status = 0, md = 0, dcolor = 0, nocaret = 0, elimit = 0, mblimit = 0, slimit = 0, showid = 0, dformat = 0, nwargs = 0, dstyle = 0, dunicode = 0;
	const char *badval = NULL;	/* an option with a value clang would reject */
	const char **wargs = NULL;
	char pmapbuf[4096];
	jmp_buf jb;
	vmreg *br = bufr = vmpool_take(&bufpool);
	buf pre = { .r = &br[0] }, cmdl = { .r = &br[1] }, incl = { .r = &br[2] }, o = { 0 };
	/* no --target: the host's own userland (Darwin on a Mac, glibc aarch64 on Linux) */
#ifdef __APPLE__
	int darwin = 1;
#else
	int darwin = 0;
#endif
	int hosted = 1, nostdinc = 0, asm_mode = 0, undef = 0;
	int march_lse = -1, mcpu_lse = -1, lse;
	int opt = 0, olevel = 0, os = 0, pie = -1, pic = 0, freestanding = 0, nobuiltin = 0, instr = 0, cfi = -1, strict_align = 0, tiny = 0, gregs = 0;
	pp_opts po = { 0 };
	char *pmap = NULL;
	double t0;
	pp *p;
	int dmacros = 0, syntax = 0, layout = 0, ms_anon = 0, short_wchar = 0, tokhash = 0, emit = 0, objmode = 0;
	int tri = -1, std = 0, strict = 0, wimpl_int = 0, wimpl_fn = 0, gnu89_inline = -1, vcc_kernel = 0;
	char objbuf[1024];

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define ARG(flag) (!strcmp(a, flag) ? (i + 1 < argc ? argv[++i] : "") : !strncmp(a, flag, strlen(flag)) ? a + strlen(flag) : NULL)
		const char *v;

		if (!strcmp(a, "--cmp") && i + 1 < argc) { cmp = argv[++i]; continue; }
		if (!strcmp(a, "-fsyntax-only")) { syntax = 1; continue; }
		if (!strcmp(a, "-S")) { syntax = 1; emit = 1; continue; }
		if (!strcmp(a, "-c")) { objmode = 1; continue; }
		if (!strcmp(a, "-dM")) { dmacros = 1; continue; }
		if (!strcmp(a, "-fms-anonymous-structs") || !strcmp(a, "-fms-extensions")) { ms_anon = 1; continue; }
		if (!strcmp(a, "-fshort-wchar")) { short_wchar = 1; continue; }
		if (!strcmp(a, "-fcolor-diagnostics") || !strcmp(a, "-fdiagnostics-color") || !strcmp(a, "-fdiagnostics-color=always")) { dcolor = 1; continue; }
		if (!strcmp(a, "-fno-color-diagnostics") || !strcmp(a, "-fno-diagnostics-color") || !strcmp(a, "-fdiagnostics-color=never")) { dcolor = -1; continue; }
		if (!strcmp(a, "-fdiagnostics-color=auto")) { dcolor = 0; continue; }
		if (!strcmp(a, "-fno-caret-diagnostics")) { nocaret = 1; continue; }
		if (!strcmp(a, "-fcaret-diagnostics")) { nocaret = 0; continue; }
		if (!strcmp(a, "-fdiagnostics-show-id")) { showid = 1; continue; }
		if (!strncmp(a, "-fdiagnostics-format=", 21)) {
			const char *f = a + 21;

			dformat = !strcmp(f, "json") ? PP_DIAG_JSON : !strcmp(f, "sarif") ? PP_DIAG_SARIF : PP_DIAG_TEXT;
			continue;
		}
		if (!strncmp(a, "-fdiagnostics-add-output=sarif:file=", 36)) { po.sarif_file = a + 36; continue; }
		if (!strncmp(a, "-fdiagnostics-add-output=html:file=", 35)) { po.html_file = a + 35; continue; }
		if (!strncmp(a, "-fdiagnostics-style=", 20)) {	/* vcc (default), clang, narrated */
			const char *f = a + 20;

			dstyle = !strcmp(f, "clang") ? PP_DSTYLE_CLANG : !strcmp(f, "narrated") ? PP_DSTYLE_NARRATED : PP_DSTYLE_VCC;
			if (dstyle == PP_DSTYLE_VCC && strcmp(f, "vcc"))
				badval = a;
			continue;
		}
		if (!strncmp(a, "-fdiagnostics-text-art-charset=", 31)) {	/* GCC's: ascii, unicode, emoji, none */
			const char *f = a + 31;

			dunicode = !strcmp(f, "unicode") || !strcmp(f, "emoji");
			if (!dunicode && strcmp(f, "ascii") && strcmp(f, "none"))
				badval = a;
			continue;
		}
		if (!strcmp(a, "-fno-diagnostics-show-id")) { showid = 0; continue; }
		if (!strcmp(a, "-fdiagnostics-parseable-fixits")) { po.diag_parseable = 1; continue; }
		if (!strcmp(a, "-fdiagnostics-generate-patch")) { po.diag_patch = 1; continue; }
		if (!strcmp(a, "--fix")) { po.fix_apply = 1; continue; }
		if (!strcmp(a, "-fno-diagnostics-parseable-fixits")) { po.diag_parseable = 0; continue; }
		if (!strncmp(a, "-fspell-checking-limit=", 23)) { slimit = atoi(a + 23); slimit = slimit ? slimit : -1; continue; }
		if (!strcmp(a, "-fno-spell-checking")) { slimit = -2; continue; }
		if (!strncmp(a, "-ferror-limit=", 14)) { elimit = atoi(a + 14); elimit = elimit ? elimit : -1; continue; }
		if (!strncmp(a, "-fmacro-backtrace-limit=", 24)) { mblimit = atoi(a + 24); mblimit = mblimit ? mblimit : -1; continue; }
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
		if (!strcmp(a, "-trigraphs") || !strcmp(a, "-ftrigraphs")) { tri = 1; continue; }
		if (!strcmp(a, "-fno-trigraphs")) { tri = 0; continue; }
		if (!strcmp(a, "-fgnu89-inline")) { gnu89_inline = 1; continue; }
		if (!strcmp(a, "-fvcc-kernel")) { vcc_kernel = 1; continue; }
		if (!strcmp(a, "-fno-gnu89-inline")) { gnu89_inline = 0; continue; }
		if ((a[1] == 'W' && a[2] && a[3] != ',') || !strcmp(a, "-w") || strstr(a, "-pedantic") == a + (a[1] == '-')) {
			/* warning options, in order, for pp_wstate (some are also handled below);
			   -pedantic is -Wpedantic, -pedantic-errors also -Werror=pedantic (two entries) */
			if (!wargs)
				wargs = calloc((size_t)argc * 2 + 1, sizeof(*wargs));
			if (!strcmp(a, "-pedantic") || !strcmp(a, "--pedantic"))
				wargs[nwargs++] = "-Wpedantic";
			else if (!strcmp(a, "-pedantic-errors") || !strcmp(a, "--pedantic-errors"))
				wargs[nwargs++] = "-Wpedantic", wargs[nwargs++] = "-Werror=pedantic";
			else if (a[1] == 'W' || a[1] == 'w')
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
		if (!strcmp(a, "-MQ") && i + 1 < argc) {	/* -MT quoted for make: $ -> $$, space -> \ space */
			static __thread char q[4096];
			const char *t = argv[++i];
			size_t k = 0;

			for (; *t && k + 3 < sizeof(q); t++) {
				if (*t == '$')
					q[k++] = '$';
				else if (*t == ' ' || *t == '\t' || *t == '#')
					q[k++] = '\\';
				q[k++] = *t;
			}
			q[k] = 0;
			deptarget = q;
			continue;
		}
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
		if ((v = ARG("-D"))) {
			if (!strncmp(v, "__KERNEL__", 10) && (!v[10] || v[10] == '='))
				vcc_kernel = 1;	/* the kernel: its error() calls rely on clang -O2 folding */
			define_arg(&cmdl, v);
			continue;
		}
		if ((v = ARG("-U"))) { bstr(&cmdl, "#undef "); bstr(&cmdl, v); bstr(&cmdl, "\n"); continue; }
		if (!strncmp(a, "-fmacro-prefix-map=", 19)) {
			snprintf(pmapbuf, sizeof(pmapbuf), "%s", a + 19);
			pmap = pmapbuf;
			continue;
		}
		if (!strncmp(a, "--target=", 9) || !strcmp(a, "-target")) { darwin = strstr(a, "darwin") || strstr(a, "apple"); hosted = 0; }
		else if (!strcmp(a, "-nostdinc")) nostdinc = 1;
		else if (!strcmp(a, "-undef")) undef = 1;
		else if (!strcmp(a, "-x") && i + 1 < argc) asm_mode = !strcmp(argv[++i], "assembler-with-cpp");
		else if (!strcmp(a, "-O0")) opt = 0, os = 0, olevel = 0;
		else if (!strcmp(a, "-Os") || !strcmp(a, "-Oz")) { opt = 2; os = 1; olevel = 2; }
		else if (!strncmp(a, "-O", 2)) opt = 2, os = 0, olevel = !strcmp(a, "-O1") || !strcmp(a, "-Og") ? 1 : 2;
		else if (!strcmp(a, "-fpie")) pie = 1, pic = 0;
		else if (!strcmp(a, "-fPIE")) pie = 2, pic = 0;
		else if (!strcmp(a, "-fpic")) pic = 1, pie = 0;
		else if (!strcmp(a, "-fPIC")) pic = 2, pie = 0;
		else if (!strcmp(a, "-fno-PIE") || !strcmp(a, "-fno-pie")) pie = 0;
		else if (!strcmp(a, "-ffreestanding")) freestanding = 1;
		else if (!strcmp(a, "-fno-builtin")) nobuiltin = 1;
		else if (!strcmp(a, "-finstrument-functions")) instr = 1;
		else if (!strcmp(a, "-fasynchronous-unwind-tables")) cfi = 1;
		else if (!strcmp(a, "-fno-asynchronous-unwind-tables")) cfi = 0;
		else if (!strcmp(a, "-mstrict-align")) strict_align = 1;
		else if (!strcmp(a, "-mgeneral-regs-only")) gregs = 1;
		else if (!strncmp(a, "-march=", 7) && arch_lse(a + 7, false) >= 0) march_lse = arch_lse(a + 7, false);
		else if (!strncmp(a, "-mcpu=", 6) && arch_lse(a + 6, true) >= 0) mcpu_lse = arch_lse(a + 6, true);
		else if (!strcmp(a, "-mcmodel=tiny")) tiny = 1;
		else if (a[0] != '-' && (strstr(a, ".c") || strstr(a, ".S") || strstr(a, ".dts"))) {
			size_t l = strlen(a);

			main_file = a;
			if (l > 2 && !strcmp(a + l - 2, ".S"))
				asm_mode = 1;
		}
	}
	(void)md;
	if (badval) {
		const char *eq = strchr(badval, '=');

		dprintf(errfd, "vcc: error: invalid value '%s' in '%.*s'\n", eq + 1, (int)(eq + 1 - badval), badval);
		status = 1;
		goto out;
	}
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
	/* hosted Linux defaults as clang's: PIE, asynchronous unwind tables (CFI) */
	{
		int hl = hosted && !darwin;

		if (pie < 0)
			pie = hl && !pic ? 2 : 0;
		if (cfi < 0)
			cfi = hl;
	}
	/* search path: -iquote, -I, -isystem, -idirafter; exact duplicates dropped */
	for (int k = 0; k < nq; k++) all[n++] = dirs_q[k];
	po.nquote = n;
#define ADD(d) do { int dup_ = 0; for (int j = po.nquote; j < n; j++) dup_ |= !strcmp(all[j], d); if (!dup_) all[n++] = d; } while (0)
	for (int k = 0; k < ni; k++) ADD(dirs_i[k]);
	n = env_dirs(cpath, all, n, po.nquote, 0);	/* CPATH: after -I, as clang */
	po.nsys = n;
	for (int k = 0; k < ns; k++) {
		/* vcc's own headers (arm_neon.h) go before the clang resource directory */
		if (strstr(dirs_s[k], "/lib/clang/") && strlen(dirs_s[k]) > 8 &&
		    !strcmp(dirs_s[k] + strlen(dirs_s[k]) - 8, "/include"))
			ADD(VCC_INCDIR);
		ADD(dirs_s[k]);
	}
	n = env_dirs(cipath, all, n, po.nquote, 1);	/* C_INCLUDE_PATH: after -isystem */
	/* Darwin: vcc's own <stdatomic.h>, <arm_neon.h>, <arm_acle.h> ahead of the SDK's */
	if (darwin && !nostdinc)
		ADD(VCC_INCDIR);
	if (darwin && !nostdinc)
		for (size_t k = 0; k < sizeof(sysdirs_darwin) / sizeof(sysdirs_darwin[0]); k++)
			ADD(sysdirs_darwin[k]);
#ifdef VCC_RESDIR
	/* hosted Linux: clang's resource headers, then glibc's (multiarch first) */
	if (hosted && !darwin && !nostdinc) {
		ADD(VCC_RESDIR);
		ADD("/usr/local/include");
		ADD("/usr/include/aarch64-linux-gnu");
		ADD("/usr/include");
	}
#endif
	for (int k = 0; k < na; k++) ADD(dirs_a[k]);
	if (darwin && !nostdinc) {
		po.fwdirs = fwdirs_darwin;
		po.nfwdirs = (int)(sizeof(fwdirs_darwin) / sizeof(fwdirs_darwin[0]));
	}
	po.dirs = all;
	po.ndirs = n;
	po.c23 = std == 23;
	po.c89 = std == 89 && strict;
	po.fatal_continue = !emit && !objmode && !syntax && !layout && !tokhash;
	po.trigraphs = tri >= 0 ? tri : strict && std != 23;	/* clang: strict modes before C23 */
	{
		const char *ep = ppd_getenv(env, "SOURCE_DATE_EPOCH");
		char *end;

		if (ep && *ep) {
			po.epoch = strtoll(ep, &end, 10);
			po.has_epoch = !*end;
		}
	}

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
	/* the Linux table is for the kernel's -mgeneral-regs-only */
	if (!darwin && !gregs && !undef)
		bstr(&pre, "#define __ARM_FP 0xE\n#define __ARM_NEON 1\n#define __ARM_NEON_FP 0xE\n");
	/* -march wins over -mcpu (clang); Darwin: Apple cores, LSE always */
	lse = darwin ? 1 : march_lse >= 0 ? march_lse : mcpu_lse >= 0 ? mcpu_lse : 0;
	if (!darwin && lse && !undef)
		bstr(&pre, "#define __ARM_FEATURE_ATOMICS 1\n");
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
	po.dsink = env->dsink;
	po.diag_color = dcolor ? dcolor : env->errtty ? 1 : 0;
	po.diag_nocaret = nocaret;
	po.diag_show_id = showid;
	po.diag_style = dstyle;
	po.diag_unicode = dunicode;
	po.diag_format = dformat;
	po.argv = argv;
	po.argc = argc;
	po.error_limit = elimit;
	po.macro_bt_limit = mblimit;
	po.spell_limit = slimit;
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
	{	/* pp_open lexes the main file: a full identifier table there ends the job too
		   (the pp it was opening is lost: a leak, as rare as the table filling) */
		static _Thread_local jmp_buf jo;

		if (setjmp(jo)) {
			vjob_jb = NULL;
			status = 1;
			goto out;
		}
		vjob_jb = &jo;
		__atomic_store_n(&vintern_full_hook, intern_full, __ATOMIC_RELAXED);	/* (every job: the same value) */
		p = pp_open(&po);
		vjob_jb = NULL;
	}
	if (!p) {
		status = 1;
		goto out;
	}
	pp_set_fatal(p, &jb);
	if (setjmp(jb)) {
		vjob_jb = NULL;
		status = 1;
		goto done;
	}
	vjob_jb = &jb;	/* as/elf overflow ends this TU too */
	__atomic_store_n(&vintern_full_hook, intern_full, __ATOMIC_RELAXED);	/* (only reaches vjob_jb in the thread that set it) */
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
				.strict_std = strict,
				.gnu89_inline = gnu89_inline, .no_builtin = freestanding || nobuiltin, .instrument = instr,
				.general_regs_only = gregs, .vcc_kernel = vcc_kernel, .lse = lse, .opt = olevel,
			       .outfd = env->outfd };
		int fd = -1;
		char outpath[4096], outtmp[4200];
		bool outrename = false;

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
			snprintf(outpath, sizeof(outpath), "%s", path);
			outrename = tmp_name(outtmp, sizeof(outtmp), path);
			fd = open(outtmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
			if (fd >= 0 && outrename)
				out_mode(fd);
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
		if (fd >= 0) {
			if (!status && client_gone())
				status = 1;	/* (the temporary is removed below) */
			if (close(fd) || (!status && outrename && rename(outtmp, outpath))) {
				dprintf(errfd, "vcc: error: cannot write %s: %s\n", out, strerror(errno));
				status = 1;
			}
			if (status && outrename)
				unlink(outtmp);
		}
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
		if (dmacros) {
			char *m;

			pp_macros(p, &m, &l);
			o.p = m;	/* freed with the process or the next job: small */
			o.n = l;
		}
	}
	if (getenv("VPP_TIME")) {
		extern _Atomic unsigned long pp_stat_try, pp_stat_skip, vdir_probes;

		dprintf(errfd, "vpp: %.1f ms, %zu bytes out; include candidates %lu, %lu ruled out by the directory cache (%lu dirs probed)\n",
			(now() - t0) * 1e3, o.n, atomic_load(&pp_stat_try), atomic_load(&pp_stat_skip),
			atomic_load(&vdir_probes));
	}
	if (pp_errors(p)) {
		/* clang: -E to stdout still prints the text; with -o it writes no file */
		if (!cmp && !objmode && (!out || !strcmp(out, "-")))
			write_out(env->cwd, out, env->outfd, o.p, o.n, errfd);
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
	vjob_jb = NULL;
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
