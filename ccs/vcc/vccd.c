/*
 * vccd: the vcc server.
 *
 * One process.  The main thread waits in kqueue for connections from the
 * shims (vcc, vld) and hands each accepted socket to a pool of I/O worker
 * threads.  A worker receives the request (argv, env, cwd, fds 0-2) into a
 * per-thread buffer, then
 *
 *   - answers toolchain probes from the capability table in caps.c,
 *     without running anything: cc-option/ld-option, as-instr (the source
 *     on stdin is looked up), user-space link tests, --version,
 *     -print-file-name, and links with options vld refuses;
 *   - runs preprocessing jobs (-E: kbuild's linker scripts and device
 *     trees) itself, with vcc's preprocessor (ppdriver.c), in this thread;
 *   - compiles -c of .c (pp.c + cc.c + emit.c) and .S (pp.c + as.c) itself,
 *     in this thread, into the .o and dependency file kbuild names; the
 *     header cache is shared by all jobs, everything else is per thread;
 *   - forwards everything else to the current backend: compiles vcc cannot
 *     do (the vDSO: -mcmodel=tiny/PIC; any TU vcc fails on) to vroomd
 *     (clang), links to vroom-ld/ld.lld.
 *
 * No allocation per request: buffers are mmap'ed once per worker.
 *
 *   vccd [--sock PATH] [--backend-sock PATH] [--ld PATH] [--lld PATH] [-j N]
 *   VCC_LOG=1   log one line per request to stderr (kind, ms, argv[0..2])
 *   VCC_PP=0    send -E jobs to the backend too (clang)
 *   VCC_CC=0    send -c jobs to the backend too (clang)
 * On SIGUSR1 (and at exit) the counts are printed: own compiles, fallbacks.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../protocol.h"
#include "caps.h"
#include "ppdriver.h"

#define MAX_STRS	(VROOM_MAX_PAYLOAD / 2)

static const char *backend_sock = "/tmp/vroomd.sock";
static const char *ld_backend;		/* full links (vroom-ld) */
static const char *lld = "/opt/homebrew/bin/ld.lld";
static bool log_requests, own_pp = true, own_cc = true;

static atomic_ulong n_probe, n_fwd, n_spawn, n_pp, n_cc, n_fallback;

/* ---------------- work queue: accepted sockets ---------------- */

#define QSIZE 4096
static int queue[QSIZE];
static unsigned qhead, qtail;
static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qcond = PTHREAD_COND_INITIALIZER;

static void queue_push(int fd)
{
	pthread_mutex_lock(&qlock);
	if (qtail - qhead == QSIZE) {
		pthread_mutex_unlock(&qlock);
		close(fd);		/* overload: the shim reports an error */
		return;
	}
	queue[qtail++ % QSIZE] = fd;
	pthread_cond_signal(&qcond);
	pthread_mutex_unlock(&qlock);
}

static int queue_pop(void)
{
	int fd;

	pthread_mutex_lock(&qlock);
	while (qhead == qtail)
		pthread_cond_wait(&qcond, &qlock);
	fd = queue[qhead++ % QSIZE];
	pthread_mutex_unlock(&qlock);
	return fd;
}

/* ---------------- request ---------------- */

struct req {
	char *raw;		/* header + payload as received */
	size_t rawlen;
	int fds[3];
	const char *cwd;
	char **argv;
	char **envp;
	int argc;
	const char *tool;	/* basename of argv[0] */
};

struct worker {
	char *raw;
	char **strs;
};

static bool read_full(int fd, char *p, size_t n)
{
	while (n) {
		ssize_t r = read(fd, p, n);

		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return false;
		p += r;
		n -= r;
	}
	return true;
}

static bool write_full(int fd, const char *p, size_t n)
{
	while (n) {
		ssize_t r = write(fd, p, n);

		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return false;
		p += r;
		n -= r;
	}
	return true;
}

static bool recv_req(struct worker *w, int conn, struct req *r)
{
	struct vroom_req_hdr *h = (struct vroom_req_hdr *)w->raw;
	char cbuf[CMSG_SPACE(3 * sizeof(int))];
	struct iovec iov = { h, sizeof(*h) };
	struct msghdr m = { 0 };
	struct cmsghdr *c;
	size_t off, i, n;

	m.msg_iov = &iov;
	m.msg_iovlen = 1;
	m.msg_control = cbuf;
	m.msg_controllen = sizeof(cbuf);
	r->fds[0] = r->fds[1] = r->fds[2] = -1;
	if (recvmsg(conn, &m, MSG_WAITALL) != (ssize_t)sizeof(*h))
		return false;
	for (c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c))
		if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
			memcpy(r->fds, CMSG_DATA(c), sizeof(r->fds));
	if (h->magic != VROOM_MAGIC || h->payload_len > VROOM_MAX_PAYLOAD ||
	    1 + h->argc + h->envc > MAX_STRS)
		return false;
	if (!read_full(conn, w->raw + sizeof(*h), h->payload_len))
		return false;
	r->raw = w->raw;
	r->rawlen = sizeof(*h) + h->payload_len;

	/* split NUL-separated strings: cwd, argv..., envp... */
	n = 0;
	for (off = sizeof(*h); off < r->rawlen && n < MAX_STRS - 2; off += strlen(w->raw + off) + 1)
		w->strs[n++] = w->raw + off;
	if (n != 1 + h->argc + h->envc || h->argc < 1)
		return false;
	r->cwd = w->strs[0];
	r->argv = w->strs + 1;
	r->argc = h->argc;
	/* argv and envp each need a NULL terminator: shift envp up by one */
	for (i = n; i > 1 + h->argc; i--)
		w->strs[i] = w->strs[i - 1];
	w->strs[1 + h->argc] = NULL;
	r->envp = w->strs + 2 + h->argc;
	r->envp[h->envc] = NULL;
	r->tool = strrchr(r->argv[0], '/') ? strrchr(r->argv[0], '/') + 1 : r->argv[0];
	return true;
}

static void say(const struct req *r, const char *fmt, const char *arg)
{
	char line[512];
	int n = snprintf(line, sizeof(line), fmt, r->tool, arg);

	if (n > 0)
		(void)!write(r->fds[2], line, n < (int)sizeof(line) ? n : (int)sizeof(line) - 1);
}

/* ---------------- probes ---------------- */

/*
 * Probes: jobs whose answer vcc's capability table gives without compiling.
 *
 *   PROBE_CC    -c/-S of C from /dev/null (cc-option, Kconfig's cc-probe):
 *               the answer depends on the options alone;
 *   PROBE_AS    -c of assembler-with-cpp from stdin into /dev/null or a
 *               try-run temporary (as-instr),
 *   PROBE_SRC   -c/-S of C from stdin into the same (`echo ... | $(CC)`):
 *               the source is looked up, and compiled for real (forwarded
 *               with the bytes already read) if it is not in the table;
 *   PROBE_LINK  a link of C from stdin into /dev/null (cc-can-link.sh).
 *
 * Anything else (a real file, stdin into a real output) is a real job.
 */
enum { PROBE_NONE, PROBE_CC, PROBE_AS, PROBE_SRC, PROBE_LINK };

static bool probe_output(const char *out)
{
	return out && (!strcmp(out, "/dev/null") || strstr(out, ".tmp_"));
}

static int cc_probe_kind(const struct req *r)
{
	bool compile = false, devnull = false, in_stdin = false;
	const char *lang = "c", *out = NULL;
	int i;

	for (i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];
		const char *why;
		int n;

		if (!strcmp(a, "-c") || !strcmp(a, "-S"))
			compile = true;
		if (!strcmp(a, "-E"))
			return PROBE_NONE;
		if (!strcmp(a, "-")) {
			if (in_stdin)
				return PROBE_NONE;
			in_stdin = true;
			continue;
		}
		if (a[0] != '-') {
			if (strcmp(a, "/dev/null"))
				return PROBE_NONE;
			devnull = true;
			continue;
		}
		n = cc_option(a, i + 1 < r->argc ? r->argv[i + 1] : NULL, &why);
		if (n == 2) {
			if (!strcmp(a, "-x"))
				lang = r->argv[i + 1];
			if (!strcmp(a, "-o"))
				out = r->argv[i + 1];
			i++;
		}
	}
	if (devnull && !in_stdin && compile && !strcmp(lang, "c"))
		return PROBE_CC;
	if (in_stdin && !devnull && compile && !strcmp(lang, "assembler-with-cpp") &&
	    probe_output(out))
		return PROBE_AS;
	if (in_stdin && !devnull && compile && !strcmp(lang, "c") && probe_output(out))
		return PROBE_SRC;
	if (in_stdin && !devnull && !compile && !strcmp(lang, "c") && out &&
	    !strcmp(out, "/dev/null"))
		return PROBE_LINK;
	return PROBE_NONE;
}

/* the probe's stdin, up to sizeof(buf); -1 if it is larger or unreadable */
static ssize_t read_stdin(const struct req *r, char *buf, size_t size)
{
	size_t n = 0;

	for (;;) {
		ssize_t k = read(r->fds[0], buf + n, size - n);

		if (k < 0 && errno == EINTR)
			continue;
		if (k < 0)
			return -1;
		if (k == 0)
			return n;
		n += k;
		if (n == size)
			return -1;
	}
}

static int forward(const struct req *r, const char *sock);

/* forward r with src[0..n) (already read from its stdin) as stdin */
static int forward_stdin(const struct req *r, const char *src, size_t n)
{
	struct req r2 = *r;
	int p[2], status;

	if (pipe(p))
		return 127;
	/* a probe source fits in the pipe buffer: write it all, then hand over */
	if (!write_full(p[1], src, n)) {
		close(p[0]);
		close(p[1]);
		return 127;
	}
	close(p[1]);
	r2.fds[0] = p[0];
	status = forward(&r2, backend_sock);
	close(p[0]);
	return status;
}

static int cc_probe(const struct req *r, int kind)
{
	const char *out = NULL, *why;
	bool werror = false;
	int i, n;

	if (kind == PROBE_LINK) {
		if (!cc_can_link) {
			say(r, "%s: error: %s\n", "cannot link user-space programs");
			return 1;
		}
	}

	for (i = 1; i < r->argc; i++)
		if (!strcmp(r->argv[i], "-Werror"))
			werror = true;
	for (i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];

		if (a[0] != '-' || !a[1])
			continue;	/* the input: /dev/null or - */
		n = cc_option(a, i + 1 < r->argc ? r->argv[i + 1] : NULL, &why);
		if (!n) {
			say(r, "%s: error: unsupported option '%s'\n", why);
			return 1;
		}
		/* -D of a macro vcc predefines: -Wmacro-redefined, an error with -Werror */
		if (werror && !strncmp(a, "-D", 2) && cc_predefined(a + 2)) {
			say(r, "%s: error: '%s' macro redefined\n", a + 2);
			return 1;
		}
		if (n == 2) {
			if (!strcmp(a, "-o"))
				out = r->argv[i + 1];
			i++;
		}
	}
	if (kind == PROBE_AS || kind == PROBE_SRC) {
		char src[4096];
		ssize_t len = read_stdin(r, src, sizeof(src));
		int ok;

		if (len < 0) {
			say(r, "%s: error: %s\n", "probe input too large");
			return 1;
		}
		ok = kind == PROBE_AS ? as_source_probe(src, len) : c_source_probe(src, len);
		if (ok < 0)
			return forward_stdin(r, src, len);
		if (!ok) {
			say(r, "%s: error: %s\n", "probe source rejected");
			return 1;
		}
	}
	/* produce the (empty) output like a real compile of /dev/null would */
	if (out && strcmp(out, "/dev/null")) {
		char path[4096];
		int fd;

		if (out[0] == '/')
			snprintf(path, sizeof(path), "%s", out);
		else
			snprintf(path, sizeof(path), "%s/%s", r->cwd, out);
		fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (fd < 0) {
			say(r, "%s: error: cannot open output '%s'\n", out);
			return 1;
		}
		close(fd);
	}
	return 0;
}

/* `ld -v <opt>...`: version query plus options; answered from the table */
static bool ld_is_probe(const struct req *r)
{
	return r->argc >= 3 && !strcmp(r->argv[1], "-v");
}

static int ld_probe(const struct req *r)
{
	const char *why;
	int i;

	for (i = 2; i < r->argc; i++)
		if (!ld_option(r->argv[i], &why)) {
			say(r, "%s: error: unknown argument '%s'\n", why);
			return 1;
		}
	return 0;
}

/* ---------------- preprocessing ---------------- */

/* -E on a named file (not stdin): vcc's preprocessor handles it */
static bool cc_is_pp(const struct req *r)
{
	bool e = false;
	const char *last = r->argv[r->argc - 1];

	for (int i = 1; i < r->argc; i++)
		e |= !strcmp(r->argv[i], "-E");
	return e && own_pp && last[0] != '-' && (strstr(last, ".c") || strstr(last, ".S") ||
						  strstr(last, ".dts") || strstr(last, ".lds"));
}

/* ---------------- compiling ---------------- */

/*
 * -c of a named .c or .S file for aarch64 Linux: vcc compiles it.  Not:
 * host programs (Mach-O), the vDSO (-mcmodel=tiny, position independent),
 * stdin, anything with -x other than c/assembler-with-cpp.
 */
static bool cc_is_compile(const struct req *r)
{
	bool c = false, linux_target = false;
	const char *last = r->argv[r->argc - 1];
	size_t n = strlen(last);

	if (!own_cc || last[0] == '-' || n < 3 || last[n - 2] != '.' || (last[n - 1] != 'c' && last[n - 1] != 'S'))
		return false;
	for (int i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];

		if (a[0] != '-')
			continue;
		if (!strcmp(a, "-c"))
			c = true;
		else if (!strncmp(a, "--target=aarch64-", 17) && strstr(a, "-linux"))
			linux_target = true;
		else if (!strcmp(a, "-E") || !strcmp(a, "-S") || !strcmp(a, "-") || !strcmp(a, "-mcmodel=tiny") ||
			 !strcmp(a, "-fpic") || !strcmp(a, "-fPIC") || !strcmp(a, "-shared") || !strcmp(a, "-emit-llvm"))
			return false;
		else if (!strcmp(a, "-x") && i + 1 < r->argc && strcmp(r->argv[i + 1], "c") &&
			 strcmp(r->argv[i + 1], "assembler-with-cpp"))
			return false;
	}
	return c && linux_target;
}

/* ---------------- forwarding ---------------- */

/* relay the request unchanged to another vroom-protocol server */
static int forward(const struct req *r, const char *sock)
{
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	char cbuf[CMSG_SPACE(3 * sizeof(int))];
	struct iovec iov = { r->raw, r->rawlen };
	struct msghdr m = { 0 };
	struct cmsghdr *c;
	int32_t status;
	ssize_t n;
	int s;

	strncpy(sa.sun_path, sock, sizeof(sa.sun_path) - 1);
	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0 || connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		say(r, "%s: error: backend %s not reachable\n", sock);
		if (s >= 0)
			close(s);
		return 127;
	}
	memset(cbuf, 0, sizeof(cbuf));
	m.msg_iov = &iov;
	m.msg_iovlen = 1;
	m.msg_control = cbuf;
	m.msg_controllen = sizeof(cbuf);
	c = CMSG_FIRSTHDR(&m);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(r->fds));
	memcpy(CMSG_DATA(c), r->fds, sizeof(r->fds));
	do
		n = sendmsg(s, &m, 0);
	while (n < 0 && errno == EINTR);
	if (n < 0 || !write_full(s, r->raw + n, r->rawlen - n) ||
	    !read_full(s, (char *)&status, sizeof(status)))
		status = 127;
	close(s);
	return status;
}

/* run a program with the request's cwd, env and fds */
static int spawn(const struct req *r, const char *prog)
{
	posix_spawn_file_actions_t fa;
	char *saved = r->argv[0];
	pid_t pid;
	int st, i, err;

	posix_spawn_file_actions_init(&fa);
	for (i = 0; i < 3; i++)
		if (r->fds[i] >= 0)
			posix_spawn_file_actions_adddup2(&fa, r->fds[i], i);
	posix_spawn_file_actions_addchdir(&fa, r->cwd);
	r->argv[0] = (char *)prog;
	err = posix_spawn(&pid, prog, &fa, NULL, r->argv, r->envp);
	r->argv[0] = saved;
	posix_spawn_file_actions_destroy(&fa);
	if (err) {
		say(r, "%s: error: cannot run %s\n", prog);
		return 127;
	}
	while (waitpid(pid, &st, 0) < 0)
		if (errno != EINTR)
			return 127;
	return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

/* ---------------- dispatch ---------------- */

static double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static int handle(const struct req *r, const char **kind)
{
	int probe;

	if (!strcmp(r->tool, "vld") || !strcmp(r->tool, "ld")) {
		if (ld_is_probe(r)) {
			*kind = "ld-probe";
			atomic_fetch_add(&n_probe, 1);
			return ld_probe(r);
		}
		if (r->argc == 2 && (!strcmp(r->argv[1], "-v") || !strcmp(r->argv[1], "--version"))) {
			*kind = "ld-version";
			atomic_fetch_add(&n_probe, 1);
			return write_full(r->fds[1], ld_version_text, strlen(ld_version_text)) ? 0 : 1;
		}
		for (int i = 1; i < r->argc; i++)
			if (ld_rejects(r->argv[i], i + 1 < r->argc ? r->argv[i + 1] : NULL)) {
				*kind = "ld-reject";
				atomic_fetch_add(&n_probe, 1);
				say(r, "%s: error: unsupported option '%s'\n",
				    strcmp(r->argv[i], "-z") ? r->argv[i] : r->argv[i + 1]);
				return 1;
			}
		/* links go to the backend linker */
		atomic_fetch_add(&n_spawn, 1);
		*kind = "ld";
		return spawn(r, ld_backend ? ld_backend : lld);
	}
	if (r->argc == 2 && !strcmp(r->argv[1], "--version")) {
		*kind = "cc-version";
		atomic_fetch_add(&n_probe, 1);
		return write_full(r->fds[1], cc_version_text, strlen(cc_version_text)) ? 0 : 1;
	}
	if (r->argc == 2 && !strncmp(r->argv[1], "-print-file-name=", 17)) {
		const char *name = r->argv[1] + 17;
		char line[4096];
		int n;

		*kind = "cc-print";
		atomic_fetch_add(&n_probe, 1);
		n = snprintf(line, sizeof(line), "%s\n", strcmp(name, "include") ? name : cc_resource_include);
		return n < (int)sizeof(line) && write_full(r->fds[1], line, n) ? 0 : 1;
	}
	probe = cc_probe_kind(r);
	if (probe) {
		static const char *const names[] = { "", "cc-probe", "as-probe", "src-probe", "link-probe" };

		*kind = names[probe];
		atomic_fetch_add(&n_probe, 1);
		return cc_probe(r, probe);
	}
	if (cc_is_pp(r)) {
		ppd_env e = { r->cwd, r->fds[1], r->fds[2] };

		*kind = "cc-E";
		atomic_fetch_add(&n_pp, 1);
		return ppd_run(r->argc, r->argv, &e);
	}
	if (cc_is_compile(r)) {
		/* diagnostics only in the log: a TU vcc fails on goes to clang, which reports */
		ppd_env e = { r->cwd, r->fds[1], log_requests ? 2 : -1 };

		if (ppd_run(r->argc, r->argv, &e) == 0) {
			*kind = "cc-own";
			atomic_fetch_add(&n_cc, 1);
			return 0;
		}
		atomic_fetch_add(&n_fallback, 1);
		if (log_requests)
			fprintf(stderr, "vccd fallback to clang: %s\n", r->argv[r->argc - 1]);
		*kind = "cc-fallbk";
	} else {
		*kind = "cc";
	}
	atomic_fetch_add(&n_fwd, 1);
	return forward(r, backend_sock);
}

static void print_counts(void)
{
	fprintf(stderr, "vccd: %lu probes, %lu -E, %lu compiled by vcc, %lu fell back to clang, %lu forwarded in all, %lu spawned\n",
		atomic_load(&n_probe), atomic_load(&n_pp), atomic_load(&n_cc), atomic_load(&n_fallback),
		atomic_load(&n_fwd), atomic_load(&n_spawn));
}

static void *worker_main(void *arg)
{
	struct worker w;

	(void)arg;
	w.raw = mmap(NULL, sizeof(struct vroom_req_hdr) + VROOM_MAX_PAYLOAD,
		     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	w.strs = mmap(NULL, MAX_STRS * sizeof(char *), PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANON, -1, 0);
	if (w.raw == MAP_FAILED || w.strs == MAP_FAILED) {
		perror("vccd: mmap");
		exit(1);
	}
	for (;;) {
		int conn = queue_pop();
		struct req r;
		int32_t status = 127;
		const char *kind = "bad";
		double t0 = now_ms();
		int i;

		if (recv_req(&w, conn, &r))
			status = handle(&r, &kind);
		(void)write_full(conn, (char *)&status, sizeof(status));
		close(conn);
		for (i = 0; i < 3; i++)
			if (r.fds[i] >= 0)
				close(r.fds[i]);
		if (log_requests) {
			double t1 = now_ms();

			/* @: start time (monotonic ms), for concurrency timelines; last: the input */
			fprintf(stderr, "vccd %-10s %7.2f ms rc=%d @%.2f %s %s\n", kind, t1 - t0, status, t0,
				kind[0] == 'b' ? "" : r.argc > 1 ? r.argv[1] : "",
				kind[0] == 'b' || r.argc < 2 ? "" : r.argv[r.argc - 1]);
		}
	}
	return NULL;
}

int main(int argc, char **argv)
{
	const char *sock = "/tmp/vccd.sock";
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	struct kevent ev;
	/* make never has more than -j requests in flight, and compiles are CPU bound:
	 * two per core leaves room for the ones that block (forwarded to vroomd,
	 * spawned tools) without a thread per possible connection */
	int nworkers = 2 * (int)sysconf(_SC_NPROCESSORS_ONLN), ls, kq, i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--sock") && i + 1 < argc)
			sock = argv[++i];
		else if (!strcmp(argv[i], "--backend-sock") && i + 1 < argc)
			backend_sock = argv[++i];
		else if (!strcmp(argv[i], "--ld") && i + 1 < argc)
			ld_backend = argv[++i];
		else if (!strcmp(argv[i], "--lld") && i + 1 < argc)
			lld = argv[++i];
		else if (!strcmp(argv[i], "-j") && i + 1 < argc)
			nworkers = atoi(argv[++i]);
		else {
			fprintf(stderr, "usage: vccd [--sock P] [--backend-sock P] [--ld P] [--lld P] [-j N]\n");
			return 2;
		}
	}
	log_requests = getenv("VCC_LOG");
	own_pp = !getenv("VCC_PP") || strcmp(getenv("VCC_PP"), "0");
	own_cc = !getenv("VCC_CC") || strcmp(getenv("VCC_CC"), "0");
	signal(SIGPIPE, SIG_IGN);

	unlink(sock);
	strncpy(sa.sun_path, sock, sizeof(sa.sun_path) - 1);
	ls = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ls < 0 || bind(ls, (struct sockaddr *)&sa, sizeof(sa)) || listen(ls, 1024)) {
		perror("vccd: socket");
		return 1;
	}
	fcntl(ls, F_SETFL, O_NONBLOCK);

	for (i = 0; i < nworkers; i++) {
		pthread_t t;
		pthread_attr_t at;

		pthread_attr_init(&at);
		pthread_attr_setstacksize(&at, 16 << 20);	/* compiles recurse (parser, emitter) */
		pthread_create(&t, &at, worker_main, NULL);
		pthread_attr_destroy(&at);
	}

	/* accept thread: kqueue, drain the backlog on every wakeup */
	kq = kqueue();
	EV_SET(&ev, ls, EVFILT_READ, EV_ADD, 0, 0, NULL);
	if (kq < 0 || kevent(kq, &ev, 1, NULL, 0, NULL) < 0) {
		perror("vccd: kqueue");
		return 1;
	}
	signal(SIGUSR1, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	EV_SET(&ev, SIGUSR1, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	kevent(kq, &ev, 1, NULL, 0, NULL);
	EV_SET(&ev, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	kevent(kq, &ev, 1, NULL, 0, NULL);
	for (;;) {
		struct kevent out;

		if (kevent(kq, NULL, 0, &out, 1, NULL) < 0) {
			if (errno == EINTR)
				continue;
			perror("vccd: kevent");
			return 1;
		}
		if (out.filter == EVFILT_SIGNAL) {
			print_counts();
			if (out.ident == SIGTERM)
				exit(0);
			continue;
		}
		for (;;) {
			int c = accept(ls, NULL, NULL);

			if (c < 0)
				break;
			fcntl(c, F_SETFL, 0);	/* BSD: accept() inherits O_NONBLOCK */
			fcntl(c, F_SETFD, FD_CLOEXEC);
			queue_push(c);
		}
	}
}
