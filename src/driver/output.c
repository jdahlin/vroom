/* Writing a job's outputs: atomic files in the client's umask, the object state pool, laying out and writing the object. */
#include "driver-internal.h"

/* path relative to cwd (if set) */
VPRIV const char *at(const char *cwd, const char *path, char *buf, size_t cap)
{
	if (!cwd || path[0] == '/')
		return path;
	snprintf(buf, cap, "%s/%s", cwd, path);
	return buf;
}

VPRIV int write_all(int fd, const char *p, size_t n)
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
VPRIV bool client_gone(void)
{
	struct pollfd pf = { ppd_conn, POLLIN, 0 };
	char c;

	return ppd_conn >= 0 && poll(&pf, 1, 0) == 1 && recv(ppd_conn, &c, 1, MSG_PEEK | MSG_DONTWAIT) == 0;
}

/* a new output's mode: the client's umask under vccd */
VPRIV void out_mode(int fd)
{
	if (ppd_umask >= 0)
		fchmod(fd, 0666 & ~(mode_t)ppd_umask);
}

VPRIV bool tmp_name(char *t, size_t cap, const char *f)
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

VPRIV int write_file(const char *cwd, const char *path, const char *s, size_t n, int errfd)
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

VPRIV int write_out(const char *cwd, const char *out, int outfd, const char *s, size_t n, int errfd)
{
	if (out && strcmp(out, "-"))
		return write_file(cwd, out, s, n, errfd);
	return write_all(outfd, s, n) ? 1 : 0;
}

/* phase times over all -c jobs (ns): front end + codegen, assembler, finish + write */
_Atomic unsigned long long ppd_ns_cc, ppd_ns_emit, ppd_ns_obj, ppd_bytes_obj, ppd_tok_out, ppd_tok_skip;
_Thread_local vperf ppd_ph[PPD_NPH];

VPRIV unsigned long long nsnow(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

/*
 * a job's object state: assembler, ELF writer, serialised output; from a
 * process-wide pool, so it outlives threads (vpp --batch starts some per pass)
 */

_Thread_local As *tl_as;
_Thread_local ElfObj *tl_obj;
_Thread_local uint8_t *tl_objbuf;
#define OBJBUF_CAP ((size_t)1 << 32)



static struct objst { As *as; ElfObj *obj; uint8_t *buf; struct objst *next; } *obj_free;
static _Thread_local struct objst *tl_os;
static pthread_mutex_t obj_mu = PTHREAD_MUTEX_INITIALIZER;


VPRIV void obj_state(void)
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


VPRIV void obj_release(void)
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

/* -c for wasm64: the object's bytes (arch/wasm/as, platform/wasm) */

_Thread_local vbuffer tl_wasm_obj;



/* lay out tl_obj and write it to out */

VPRIV int write_obj(const char *cwd, const char *out, int outfd, int errfd, int macho)
{
	unsigned long long t0 = nsnow();
	vperf v0 = vperformance_now();
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
VPRIV const char *obj_name(const char *main_file, char *buf, size_t cap)
{
	const char *b = strrchr(main_file, '/') ? strrchr(main_file, '/') + 1 : main_file;
	const char *dot = strrchr(b, '.');

	snprintf(buf, cap, "%.*s.o", (int)(dot ? dot - b : (long)strlen(b)), b);
	return buf;
}
