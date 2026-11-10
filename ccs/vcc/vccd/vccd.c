/*
 * vccd: the vcc server.
 *
 * One process.  The main thread waits in kqueue (epoll on Linux) for connections from the
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
 *   - compiles -c of .c (pp/ + cc/ + emit/) and .S (pp/ + as.c) itself,
 *     in this thread, into the .o and dependency file kbuild names; the
 *     header cache is shared by all jobs, everything else is per thread;
 *   - forwards everything else to the current backend: compiles vcc cannot
 *     do (the vDSO: -mcmodel=tiny/PIC; any TU vcc fails on) to vroomd
 *     (clang), links to vroom-ld/ld.lld;
 *   - runs the archiver (ar, var, llvm-ar by argv[0]) itself: ar.c.
 *   - with VCC_DELEGATE=PROG in the request's environment (a build that is
 *     not the kernel, e.g. PostgreSQL on macOS: bench/pg/build.sh vccd),
 *     a vcc request is either -c of a .c file, compiled here, or run as
 *     PROG with the same arguments (probes, links, --version, and any TU
 *     vcc fails on); sources containing $VCC_DELEGATE_SKIP go to PROG too;
 *   - runs make's recipes for vsh (argv[0] sh): rsh.c interprets kbuild's
 *     subset of sh and runs the tools in it as requests here; anything else
 *     is answered VROOM_STATUS_DECLINE and vsh runs the real shell.
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
#if defined(__GLIBC__) && !defined(__APPLE__)
#define posix_spawn_file_actions_addchdir posix_spawn_file_actions_addchdir_np
#endif
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <sys/event.h>
#else
#include <sys/epoll.h>
#include <sys/signalfd.h>
#endif
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <poll.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif
#include "../../protocol.h"
#include "caps.h"
#include "../driver/ccsnap.h"
#include "../pp/pp.h"
#include "../lex/lex.h"
#include "../lib/intern.h"
#include "../pp/dircache.h"
#include "../driver/ppdriver.h"
#include "fixdep.h"
#include "ar.h"
#include "rsh.h"

#define MAX_STRS	(VROOM_MAX_PAYLOAD / 2)

static const char *backend_sock = "/tmp/vroomd.sock";
static const char *ld_backend;		/* full links (vroom-ld) */
#ifdef VROOM_LLVM_BIN
static const char *lld = VROOM_LLVM_BIN "/ld.lld";
#else
static const char *lld = "/opt/homebrew/bin/ld.lld";
#endif
static bool log_requests, own_pp = true, own_cc = true;

static atomic_ulong n_probe, n_fwd, n_spawn, n_pp, n_cc, n_fallback, n_ar, n_sh, n_sh_decl;
static char self_exe[4096];	/* our own binary (realpath), for a restart */
static uint64_t self_build;	/* vroom_build_id of our own binary */
static char tool_dir[4096];	/* where vccd and its shims live (realpath) */

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

static _Atomic int n_active;

/*
 * Builds.  The caches answer "what is at this path" for the server's life;
 * between builds files change.  A request from a new process group is a new
 * make invocation: before it runs, the running jobs finish and the file,
 * directory and include caches are emptied (old entries stay allocated, so
 * nothing in flight dangles).  Jobs of the same group never wait.
 */
static pthread_mutex_t gen_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gen_cv = PTHREAD_COND_INITIALIZER;
static uint32_t gen_pgrp;
static int gen_running, gen_flushing;
static _Atomic unsigned long n_flush;

static void print_counts(void);
static int g_argc, g_ls = -1;
static char **g_argv;
static const char *g_sock;
static _Atomic int gen_restart;

/* resident memory in MB */
static size_t rss_mb(void)
{
#ifdef __APPLE__
	struct mach_task_basic_info ti;
	mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;

	if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&ti, &n) != KERN_SUCCESS)
		return 0;
	return (size_t)(ti.resident_size >> 20);
#else
	long pages = 0, res = 0;
	FILE *f = fopen("/proc/self/statm", "r");

	if (f && fscanf(f, "%ld %ld", &pages, &res) != 2)
		res = 0;
	if (f)
		fclose(f);
	return (size_t)res * (size_t)sysconf(_SC_PAGESIZE) >> 20;
#endif
}

/*
 * Memory: every cache only grows.  At a build boundary, with no job running,
 * a server above VCC_RESTART_MB resident (default 6144) starts over: every
 * request it holds or has queued is answered VROOM_STATUS_RETRY (the shim
 * sends it again), then it execs itself on the same listening socket.
 * restart_due() decides (gen_enter, idle); the worker that saw it answers its
 * own request and calls do_restart(), which returns only if exec fails.
 */
static bool restart_due(void)
{
	const char *e = getenv("VCC_RESTART_MB");
	size_t lim = e ? strtoul(e, NULL, 10) : 6144;

	return g_ls >= 0 && self_exe[0] && rss_mb() >= lim;
}

/* the accept loop: during a restart it accepts nothing more (connections
   wait in the kernel's backlog for the new process) */
static _Atomic int accept_parked;

static void park_accept(void)
{
	if (!atomic_load(&gen_restart))
		return;
	atomic_store(&accept_parked, 1);
	while (atomic_load(&gen_restart))	/* (until the exec, or its failure) */
		usleep(1000);
	atomic_store(&accept_parked, 0);
}

static void do_restart(void)
{
	while (!atomic_load(&accept_parked))
		usleep(1000);
	for (;;) {	/* the other workers answer what they took (n_active counts us too) */
		pthread_mutex_lock(&qlock);
		if (atomic_load(&n_active) == 1) {
			while (qhead != qtail) {	/* still queued: never read */
				int c = queue[qhead++ % QSIZE];
				int32_t st = (int32_t)VROOM_STATUS_RETRY;

				(void)!write(c, &st, sizeof(st));
				close(c);
			}
			break;	/* (qlock stays held: nobody takes another) */
		}
		pthread_mutex_unlock(&qlock);
		usleep(1000);
	}
	{
		char fd[16], **av = calloc((size_t)g_argc + 3, sizeof(char *));
		int k = 0;

		for (int i = 0; i < g_argc; i++) {
			if (!strcmp(g_argv[i], "--listen-fd")) {
				i++;
				continue;
			}
			av[k++] = g_argv[i];
		}
		snprintf(fd, sizeof(fd), "%d", g_ls);
		av[k++] = "--listen-fd", av[k++] = fd, av[k] = NULL;
		fprintf(stderr, "vccd: restarting (%zu MB resident; identifier table %s)\n", rss_mb(),
			atomic_load(&vtables_full) ? "full" : "not full");
		print_counts();
		fcntl(g_ls, F_SETFD, 0);
		fcntl(g_ls, F_SETFL, 0);
		execv(self_exe, av);
		perror("vccd: restart");	/* go on with the old process */
		fcntl(g_ls, F_SETFD, FD_CLOEXEC);
		fcntl(g_ls, F_SETFL, O_NONBLOCK);
		atomic_store(&gen_restart, 0);
		pthread_mutex_unlock(&qlock);
	}
}

/* 1: answer VROOM_STATUS_RETRY (the server is restarting); 2: the same, and
   then restart (do_restart) */
static int gen_enter(uint32_t pgrp)
{
	pthread_mutex_lock(&gen_mu);
	while (gen_flushing && !atomic_load(&gen_restart))
		pthread_cond_wait(&gen_cv, &gen_mu);
	if (atomic_load(&gen_restart)) {
		pthread_mutex_unlock(&gen_mu);
		return 1;
	}
	if (atomic_load(&vtables_full) && !gen_running && g_ls >= 0 && self_exe[0]) {
		/* identifiers are never freed: a full table fails every later TU */
		atomic_store(&gen_restart, 1);
		pthread_cond_broadcast(&gen_cv);
		pthread_mutex_unlock(&gen_mu);
		return 2;
	}
	if (pgrp && pgrp != gen_pgrp) {
		if (gen_pgrp) {
			gen_flushing = 1;
			while (gen_running)
				pthread_cond_wait(&gen_cv, &gen_mu);
			if (restart_due()) {
				atomic_store(&gen_restart, 1);
				gen_flushing = 0;
				pthread_cond_broadcast(&gen_cv);	/* (waiters see gen_restart) */
				pthread_mutex_unlock(&gen_mu);
				return 2;
			}
			vlex_cache_flush();
			vdir_flush();
			pp_include_flush();
			atomic_fetch_add(&n_flush, 1);
			gen_flushing = 0;
			pthread_cond_broadcast(&gen_cv);
		}
		gen_pgrp = pgrp;
	}
	gen_running++;
	pthread_mutex_unlock(&gen_mu);
	return 0;
}

static void gen_leave(void)
{
	pthread_mutex_lock(&gen_mu);
	if (!--gen_running)
		pthread_cond_broadcast(&gen_cv);
	pthread_mutex_unlock(&gen_mu);
}	/* connections taken by a worker, not yet answered */

static int queue_pop(void)
{
	int fd;

	pthread_mutex_lock(&qlock);
	while (qhead == qtail)
		pthread_cond_wait(&qcond, &qlock);
	fd = queue[qhead++ % QSIZE];
	atomic_fetch_add(&n_active, 1);	/* under the lock: drain_exit never sees it in neither place */
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

static bool split_req(char *raw, char **strs, struct req *r);

static bool recv_req(struct worker *w, int conn, struct req *r)
{
	struct vroom_req_hdr *h = (struct vroom_req_hdr *)w->raw;
	char cbuf[CMSG_SPACE(16 * sizeof(int))] = { 0 };
	struct iovec iov = { h, sizeof(*h) };
	int nfd = 0, bad = 0;
	struct msghdr m = { 0 };
	struct cmsghdr *c;

	m.msg_iov = &iov;
	m.msg_iovlen = 1;
	m.msg_control = cbuf;
	m.msg_controllen = sizeof(cbuf);
	r->fds[0] = r->fds[1] = r->fds[2] = -1;
	struct pollfd pf = { conn, POLLIN, 0 };
	/* the shim sends right after connecting: a silent client frees the worker
	   (macOS: SO_RCVTIMEO does not bound recvmsg with MSG_WAITALL) */
	ssize_t got = poll(&pf, 1, 2000) == 1 ? recvmsg(conn, &m, MSG_WAITALL) : -1;

	/* exactly three fds (stdin, stdout, stderr); any other count closes all we got */
	if (got < 0)
		m.msg_controllen = 0;
	for (c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c)) {
		if (c->cmsg_len < CMSG_LEN(0))	/* (CMSG_NXTHDR would not advance) */
			break;
		if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS)
			continue;
		size_t k = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);

		for (size_t i = 0; i < k; i++) {
			int fd;

			memcpy(&fd, CMSG_DATA(c) + i * sizeof(int), sizeof(int));
			fcntl(fd, F_SETFD, FD_CLOEXEC);
			if (nfd < 3)
				r->fds[nfd] = fd;
			else
				close(fd), bad = 1;
			nfd++;
		}
	}
	if (bad || nfd != 3 || (m.msg_flags & MSG_CTRUNC) || got != (ssize_t)sizeof(*h)) {
		for (int i = 0; i < 3; i++)
			if (r->fds[i] >= 0)
				close(r->fds[i]), r->fds[i] = -1;
		return false;
	}
	if (h->magic != VROOM_MAGIC || h->payload_len > VROOM_MAX_PAYLOAD ||
	    1 + h->argc + h->envc > MAX_STRS)
		return false;
	if (!read_full(conn, w->raw + sizeof(*h), h->payload_len))
		return false;
	return split_req(w->raw, w->strs, r);
}

/* the strings of a request in raw (header + payload): cwd, argv..., envp... */
static bool split_req(char *raw, char **strs, struct req *r)
{
	struct vroom_req_hdr *h = (struct vroom_req_hdr *)raw;
	size_t off, i, n;

	r->raw = raw;
	r->rawlen = sizeof(*h) + h->payload_len;

	/* split NUL-separated strings: cwd, argv..., envp... */
	n = 0;
	for (off = sizeof(*h); off < r->rawlen && n < MAX_STRS - 2; off += strlen(raw + off) + 1)
		strs[n++] = raw + off;
	if (n != 1 + h->argc + h->envc || h->argc < 1)
		return false;
	r->cwd = strs[0];
	r->argv = strs + 1;
	r->argc = h->argc;
	/* argv and envp each need a NULL terminator: shift envp up by one */
	for (i = n; i > 1 + h->argc; i--)
		strs[i] = strs[i - 1];
	strs[1 + h->argc] = NULL;
	r->envp = strs + 2 + h->argc;
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
/* options whose value is the next argument (so it is not an input) */
static bool takes_value(const char *a)
{
	static const char *const v[] = {
		"-o", "-I", "-D", "-U", "-include", "-imacros", "-isystem", "-idirafter", "-iquote", "-iprefix",
		"-iwithprefix", "-isysroot", "-MF", "-MT", "-MQ", "-x", "-Xclang", "-Xassembler", "-Xpreprocessor",
		"-mllvm", "--param", "-arch", "-target", "-include-pch", "--sysroot",
	};

	for (size_t k = 0; k < sizeof v / sizeof *v; k++)
		if (!strcmp(a, v[k]))
			return true;
	return false;
}

/* the one input of a compile: a .c or .S file anywhere on the line, NULL if none or several */
static const char *cc_input(const struct req *r)
{
	const char *in = NULL;

	for (int i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];
		size_t n = strlen(a);

		if (a[0] == '-') {
			if (takes_value(a))
				i++;
			continue;
		}
		if (n < 3 || a[n - 2] != '.' || (a[n - 1] != 'c' && a[n - 1] != 'S') || in)
			return NULL;	/* another kind of input (.o, .s, ...) or a second one */
		in = a;
	}
	return in;
}

static bool cc_is_compile(const struct req *r)
{
	bool c = false, linux_target = false;

	if (!own_cc || !cc_input(r))
		return false;
	for (int i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];

		if (a[0] != '-')
			continue;
		if (!strcmp(a, "-c"))
			c = true;
		else if (!strncmp(a, "--target=aarch64-", 17) && strstr(a, "-linux"))
			linux_target = true;
		else if (!strcmp(a, "-S"))
			c = true;	/* assembly text: the same job */
		else if (!strcmp(a, "-E") || !strcmp(a, "-") || !strcmp(a, "-mcmodel=tiny") ||
			 !strcmp(a, "-shared") || !strcmp(a, "-emit-llvm"))
			return false;
		else if (!strcmp(a, "-x") && i + 1 < r->argc && strcmp(r->argv[i + 1], "c") &&
			 strcmp(r->argv[i + 1], "assembler-with-cpp"))
			return false;
		else if (takes_value(a))
			i++;
	}
	return c && linux_target;
}

/* VCC_DELEGATE: -c of a named .c file (the last argument), nothing vcc's driver would not do */
static bool dg_is_compile(const struct req *r, const char *skip)
{
	const char *last = r->argv[r->argc - 1];
	size_t n = strlen(last);
	bool c = false;

	if (!own_cc || last[0] == '-' || n < 3 || strcmp(last + n - 2, ".c") || (skip && *skip && strstr(last, skip)))
		return false;
	for (int i = 1; i < r->argc - 1; i++) {
		const char *a = r->argv[i];

		if (!strcmp(a, "-c"))
			c = true;
		else if (!strcmp(a, "-E") || !strcmp(a, "-S") || !strcmp(a, "-M") || !strcmp(a, "-MM") || !strcmp(a, "-x") ||
			 !strcmp(a, "-") || !strcmp(a, "-emit-llvm") || !strncmp(a, "-print-", 7) || !strcmp(a, "-###"))
			return false;
	}
	return c;
}

static int spawn(const struct req *r, const char *prog);
static const char *req_env(const struct req *r, const char *name);

static int delegate(const struct req *r, const char *prog, const char **kind)
{
	if (dg_is_compile(r, req_env(r, "VCC_DELEGATE_SKIP"))) {
		/* diagnostics only in the log: PROG runs a failed TU again and reports */
		ppd_env e = { r->cwd, r->fds[1], log_requests ? 2 : -1, r->envp };

		if (ppd_run(r->argc, r->argv, &e) == 0) {
			*kind = "cc-own";
			atomic_fetch_add(&n_cc, 1);
			return 0;
		}
		atomic_fetch_add(&n_fallback, 1);
		if (log_requests)
			fprintf(stderr, "vccd fallback to %s: %s\n", prog, r->argv[r->argc - 1]);
		*kind = "dg-fallbk";
	} else {
		*kind = "dg";
	}
	atomic_fetch_add(&n_spawn, 1);
	return spawn(r, prog);
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

	/* the request's environment minus the dynamic loader's: a client's
	   DYLD_INSERT_LIBRARIES / LD_PRELOAD must not run code in our children */
	size_t ne = 0, nall = 0;
	char **env;

	while (r->envp[nall])
		nall++;
	env = malloc((nall + 1) * sizeof(char *));
	for (char **e = r->envp; *e; e++)
		if (strncmp(*e, "DYLD_", 5) && strncmp(*e, "LD_PRELOAD=", 11) && strncmp(*e, "LD_LIBRARY_PATH=", 16) &&
		    strncmp(*e, "LD_AUDIT=", 9) && strncmp(*e, "VCC_UMASK=", 10))
			env[ne++] = *e;
	env[ne] = NULL;
	posix_spawn_file_actions_init(&fa);
	for (i = 0; i < 3; i++)
		if (r->fds[i] >= 0)
			posix_spawn_file_actions_adddup2(&fa, r->fds[i], i);
	posix_spawn_file_actions_addchdir(&fa, r->cwd);
	r->argv[0] = (char *)prog;
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
	/* only 0-2 reach the child: other workers' request fds are open meanwhile */
	posix_spawnattr_t sa;

	posix_spawnattr_init(&sa);
	posix_spawnattr_setflags(&sa, POSIX_SPAWN_CLOEXEC_DEFAULT);
	err = posix_spawn(&pid, prog, &fa, &sa, r->argv, env);
	posix_spawnattr_destroy(&sa);
#else
	err = posix_spawn(&pid, prog, &fa, NULL, r->argv, env);
#endif
	r->argv[0] = saved;
	free(env);
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

static const char *req_env(const struct req *r, const char *name)
{
	size_t n = strlen(name);

	for (char **e = r->envp; *e; e++)
		if (!strncmp(*e, name, n) && (*e)[n] == '=')
			return *e + n + 1;
	return NULL;
}

/*
 * Kbuild's cmd_and_fixdep done here (patches/0008): a compile whose request
 * carries VCC_FIXDEP_D/T/O/CMD (dep file, target, .cmd file, saved command
 * line) gets fixdep's .cmd written and its dep file removed once it has
 * succeeded, so the recipe runs no fixdep and no rm.
 */
static int fixdep_after(const struct req *r, int status)
{
	const char *d = req_env(r, "VCC_FIXDEP_D"), *t, *o, *c;

	if (status || !d)
		return status;
	t = req_env(r, "VCC_FIXDEP_T");
	o = req_env(r, "VCC_FIXDEP_O");
	c = req_env(r, "VCC_FIXDEP_CMD");
	if (!t || !o || !c) {
		say(r, "vccd: %s without VCC_FIXDEP_T/O/CMD\n", "VCC_FIXDEP_D");
		return 2;
	}
	return vfixdep(r->cwd, d, t, c, o, r->fds[2]);
}

static int handle1(const struct req *r, const char **kind);

static int handle(const struct req *r, const char **kind)
{
	int status;

	ppd_umask = -1;	/* the shim sends VCC_UMASK; outputs then get 0666 & ~umask */
	for (char **e = r->envp; *e; e++)
		if (!strncmp(*e, "VCC_UMASK=", 10))
			ppd_umask = (int)(strtoul(*e + 10, NULL, 8) & 0777);
	status = handle1(r, kind);

	if (!strcmp(*kind, "cc-own") || !strcmp(*kind, "cc") || !strcmp(*kind, "cc-fallbk"))
		status = fixdep_after(r, status);
	return status;
}

static int run_sh(const struct req *r);

static int handle1(const struct req *r, const char **kind)
{
	int probe;

	if (!strcmp(r->tool, "sh")) {
		/* vsh: a recipe, run here if rsh knows all of it */
		int st = run_sh(r);

		*kind = st == RSH_DECLINE ? "sh-decl" : "sh";
		atomic_fetch_add(st == RSH_DECLINE ? &n_sh_decl : &n_sh, 1);
		return st == RSH_DECLINE ? VROOM_STATUS_DECLINE : st;
	}
	if (!strcmp(r->tool, "vcc")) {
		const char *dg = req_env(r, "VCC_DELEGATE");

		if (dg && *dg)
			return delegate(r, dg, kind);
	}
	if (!strcmp(r->tool, "var") || !strcmp(r->tool, "ar") || !strcmp(r->tool, "llvm-ar")) {
		/* the archiver: thin archives written here (ar.c), never llvm-ar */
		*kind = "ar";
		atomic_fetch_add(&n_ar, 1);
		return ar_run(r->cwd, r->argc, r->argv, r->fds[1], r->fds[2]);
	}
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
		ppd_env e = { r->cwd, r->fds[1], r->fds[2], r->envp };

		*kind = "cc-E";
		atomic_fetch_add(&n_pp, 1);
		return ppd_run(r->argc, r->argv, &e);
	}
	if (cc_is_compile(r)) {
		/* diagnostics only in the log: a TU vcc fails on goes to clang, which reports */
		ppd_env e = { r->cwd, r->fds[1], log_requests ? 2 : -1, r->envp };

		if (ppd_run(r->argc, r->argv, &e) == 0) {
			*kind = "cc-own";
			atomic_fetch_add(&n_cc, 1);
			return 0;
		}
		atomic_fetch_add(&n_fallback, 1);
		if (log_requests)
			fprintf(stderr, "vccd fallback to clang: %s\n", cc_input(r));
		*kind = "cc-fallbk";
	} else {
		*kind = "cc";
	}
	atomic_fetch_add(&n_fwd, 1);
	return forward(r, backend_sock);
}

/* ---------------- recipes (vsh, rsh.c) ---------------- */

static void log_req(const struct req *r, const char *kind, int status, double t0)
{
	if (log_requests) {
		double t1 = now_ms();

		/* @: start time (monotonic ms), for concurrency timelines; last: the input */
		fprintf(stderr, "vccd %-10s %7.2f ms rc=%d @%.2f %s %.200s\n", kind, t1 - t0, status, t0,
			kind[0] == 'b' ? "" : r->argc > 1 ? r->argv[1] : "",
			kind[0] == 'b' || r->argc < 2 ? "" : r->argv[r->argc - 1]);
	}
}

/* a shim of ours: vcc, vas, vld or var next to vccd */
static bool sh_is_tool(void *ctx, const char *argv0)
{
	const char *b = strrchr(argv0, '/');
	char dir[4096], real[4096];
	size_t n;

	(void)ctx;
	if (!b || !*tool_dir || (strcmp(b + 1, "vcc") && strcmp(b + 1, "vas") &&
				 strcmp(b + 1, "vld") && strcmp(b + 1, "var")))
		return false;
	n = b - argv0;
	if (n == strlen(tool_dir) && !memcmp(argv0, tool_dir, n))
		return true;
	if (n >= sizeof(dir))
		return false;
	memcpy(dir, argv0, n);
	dir[n] = 0;
	return realpath(dir, real) && !strcmp(real, tool_dir);
}

struct sh_ctx {
	const struct req *r;	/* the vsh request: cwd and env */
};

/* per thread: the request a recipe's tool call is turned into */
static _Thread_local char *sh_raw;
static _Thread_local char **sh_strs;

static int sh_run_tool(void *ctx, char **argv, char **assign, const int fds[3])
{
	const struct req *r = ((struct sh_ctx *)ctx)->r;
	struct vroom_req_hdr *h;
	struct req q;
	const char *kind = "bad";
	double t0 = now_ms();
	char *p, *end;
	int st, argc = 0, envc = 0;

	if (!sh_raw) {
		sh_raw = mmap(NULL, sizeof(struct vroom_req_hdr) + VROOM_MAX_PAYLOAD,
			      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		sh_strs = mmap(NULL, MAX_STRS * sizeof(char *), PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANON, -1, 0);
		if (sh_raw == MAP_FAILED || sh_strs == MAP_FAILED) {
			sh_raw = NULL;
			say(r, "%s: error: %s\n", "out of memory");
			return 127;
		}
	}
	h = (struct vroom_req_hdr *)sh_raw;
	p = sh_raw + sizeof(*h);
	end = p + VROOM_MAX_PAYLOAD;
#define PUT(s) do { size_t n_ = strlen(s) + 1; if (p + n_ > end) goto big; memcpy(p, s, n_); p += n_; } while (0)
	PUT(r->cwd);
	for (; argv[argc]; argc++)
		PUT(argv[argc]);
	/* the recipe's NAME=VALUE first, then the shell's environment without those names */
	for (char **a = assign; *a; a++, envc++)
		PUT(*a);
	for (char **e = r->envp; *e; e++) {
		bool dup = false;

		for (char **a = assign; *a && !dup; a++) {
			size_t n = strchr(*a, '=') - *a + 1;

			dup = !strncmp(*e, *a, n);
		}
		if (!dup) {
			PUT(*e);
			envc++;
		}
	}
#undef PUT
	h->magic = VROOM_MAGIC;
	h->argc = argc;
	h->envc = envc;
	h->payload_len = p - (sh_raw + sizeof(*h));
	if (!split_req(sh_raw, sh_strs, &q)) {
		say(r, "%s: error: %s\n", "bad tool call in recipe");
		return 127;
	}
	memcpy(q.fds, fds, sizeof(q.fds));
	st = handle(&q, &kind);
	log_req(&q, kind, st, t0);
	return st;
big:
	say(r, "%s: error: %s\n", "recipe tool call too large");
	return 127;
}

static int sh_fixdep(void *ctx, const char *dep, const char *target, const char *cmd,
		     const char *cmdfile, int errfd)
{
	return vfixdep(((struct sh_ctx *)ctx)->r->cwd, dep, target, cmd, cmdfile, errfd);
}

/* argv: sh -c SCRIPT or sh -ec SCRIPT (make's .SHELLFLAGS); anything else is the shell's */
static int run_sh(const struct req *r)
{
	struct sh_ctx ctx = { r };
	struct rsh_ops ops = { &ctx, sh_is_tool, sh_run_tool, sh_fixdep };

	if (r->argc != 3 || (strcmp(r->argv[1], "-c") && strcmp(r->argv[1], "-ec")))
		return RSH_DECLINE;
	return rsh_run(r->cwd, r->argv[2], r->argv[1][1] == 'e', r->fds, &ops);
}

static void print_counts(void)
{
	fprintf(stderr, "vccd: %lu probes, %lu -E, %lu compiled by vcc, %lu fell back to clang, %lu forwarded in all, %lu spawned, %lu ar, %lu recipes run, %lu passed to sh; %lu builds (cache flushes)\n",
		atomic_load(&n_probe), atomic_load(&n_pp), atomic_load(&n_cc), atomic_load(&n_fallback),
		atomic_load(&n_fwd), atomic_load(&n_spawn), atomic_load(&n_ar),
		atomic_load(&n_sh), atomic_load(&n_sh_decl), atomic_load(&n_flush));
	{	/* the shared caches, as vpp --batch prints them */
		char hs[512];
		struct rusage ru;

		pp_hc_stats(hs, sizeof(hs));
		fprintf(stderr, "vccd: %s\n", hs);
		dc_stats(hs, sizeof(hs));
		fprintf(stderr, "vccd: %s\n", hs);
		getrusage(RUSAGE_SELF, &ru);
		fprintf(stderr, "vccd: cpu user %.2f s sys %.2f s, max rss %.0f MB\n",
			ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6, ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6,
#ifdef __APPLE__
			ru.ru_maxrss / 1048576.0);
#else
			ru.ru_maxrss / 1024.0);
#endif
	}
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
		int conn = queue_pop();	/* (counts itself in n_active) */
		struct req r;
		int32_t status = 127;
		const char *kind = "bad";
		double t0 = now_ms();
		int i;

		if (recv_req(&w, conn, &r)) {
			struct vroom_req_hdr *h = (struct vroom_req_hdr *)w.raw;

			if (h->build && self_build && h->build != self_build) {
				status = (int32_t)VROOM_STATUS_MISMATCH, kind = "mismatch";
			} else {
				int g = gen_enter(h->pgrp);

				if (g) {
					status = (int32_t)VROOM_STATUS_RETRY, kind = "retry";
					if (g == 2) {	/* answer first: the exec drops every fd we hold */
						(void)write_full(conn, (char *)&status, sizeof(status));
						close(conn);
						for (i = 0; i < 3; i++)
							if (r.fds[i] >= 0)
								close(r.fds[i]), r.fds[i] = -1;
						do_restart();
						conn = open("/dev/null", O_RDWR);	/* (exec failed: the loop's close) */
					}
				} else {
					ppd_conn = conn;
					status = handle(&r, &kind);
					ppd_conn = -1;
					gen_leave();
				}
			}
		}
		(void)write_full(conn, (char *)&status, sizeof(status));
		close(conn);
		for (i = 0; i < 3; i++)
			if (r.fds[i] >= 0)
				close(r.fds[i]);
		log_req(&r, kind, status, t0);
		atomic_fetch_sub(&n_active, 1);
	}
	return NULL;
}

/* SIGTERM: no new connections; the queued and running jobs finish, then exit */
static __attribute__((noreturn)) void drain_exit(int ls, const char *sock)
{
	close(ls);
	unlink(sock);
	for (;;) {
		pthread_mutex_lock(&qlock);
		bool idle = qhead == qtail && !atomic_load(&n_active);
		pthread_mutex_unlock(&qlock);
		if (idle)
			exit(0);
		usleep(5000);
	}
}

static bool peer_is_us(int c)
{
#ifdef __APPLE__
	uid_t u;
	gid_t g;

	return !getpeereid(c, &u, &g) && u == geteuid();
#else
	struct ucred cr;
	socklen_t l = sizeof(cr);

	return !getsockopt(c, SOL_SOCKET, SO_PEERCRED, &cr, &l) && cr.uid == geteuid();
#endif
}

int main(int argc, char **argv)
{
	const char *sock = NULL;
	char ddir[1024], dsock[1100];
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
#ifdef __APPLE__
	struct kevent ev;
#endif
	/* make never has more than -j requests in flight, and compiles are CPU bound:
	 * two per core leaves room for the ones that block (forwarded to vroomd,
	 * spawned tools) without a thread per possible connection */
	int nworkers = 2 * (int)sysconf(_SC_NPROCESSORS_ONLN), ls, kq, i, listen_fd = -1;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--sock") && i + 1 < argc)
			sock = argv[++i];
		else if (!strcmp(argv[i], "--listen-fd") && i + 1 < argc)
			listen_fd = atoi(argv[++i]);
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
	{	/* the shims next to vccd: recipes may run them here (rsh) */
		char self[4096], *sl;

		if (realpath(argv[0], self) && (sl = strrchr(self, '/'))) {
			self_build = vroom_build_id(self);
			snprintf(self_exe, sizeof(self_exe), "%s", self);
			*sl = 0;
			snprintf(tool_dir, sizeof(tool_dir), "%s", self);
		}
	}
	own_pp = !getenv("VCC_PP") || strcmp(getenv("VCC_PP"), "0");
	own_cc = !getenv("VCC_CC") || strcmp(getenv("VCC_CC"), "0");
	signal(SIGPIPE, SIG_IGN);

	if (!sock) {
		if (vroom_default_sock(ddir, sizeof(ddir), dsock, sizeof(dsock), 1)) {
			fprintf(stderr, "vccd: %s is not a private directory of ours\n", ddir);
			return 1;
		}
		sock = dsock;
	}
	g_argc = argc, g_argv = argv, g_sock = sock;
	if (listen_fd >= 0) {	/* re-exec'ed after a memory restart: the same socket */
		ls = g_ls = listen_fd;
		fcntl(ls, F_SETFD, FD_CLOEXEC);
		goto listening;
	}
	unlink(sock);
	strncpy(sa.sun_path, sock, sizeof(sa.sun_path) - 1);
	ls = socket(AF_UNIX, SOCK_STREAM, 0);
	g_ls = ls;
	{	/* the socket is 0600 whatever the umask: the peer check below is the real gate */
		mode_t um = umask(077);
		int bad = ls < 0 || bind(ls, (struct sockaddr *)&sa, sizeof(sa));

		umask(um);
		if (bad || listen(ls, 1024)) {
			perror("vccd: socket");
			return 1;
		}
	}
listening:
	fcntl(ls, F_SETFL, O_NONBLOCK);
#ifndef __APPLE__
	{	/* USR1/TERM go to the signalfd below: block them before the workers inherit the mask */
		sigset_t ss;

		sigemptyset(&ss);
		sigaddset(&ss, SIGUSR1);
		sigaddset(&ss, SIGTERM);
		pthread_sigmask(SIG_BLOCK, &ss, NULL);
	}
#endif

	for (i = 0; i < nworkers; i++) {
		pthread_t t;
		pthread_attr_t at;

		pthread_attr_init(&at);
		pthread_attr_setstacksize(&at, 16 << 20);	/* compiles recurse (parser, emitter) */
		pthread_create(&t, &at, worker_main, NULL);
		pthread_attr_destroy(&at);
	}

	/* accept thread: kqueue (epoll on Linux), drain the backlog on every wakeup */
#ifdef __APPLE__
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
		struct timespec tick = { 0, 50000000 };	/* (a restart waits for park_accept) */
		int nev;

		park_accept();
		if ((nev = kevent(kq, NULL, 0, &out, 1, &tick)) == 0)
			continue;
		if (nev < 0) {
			if (errno == EINTR)
				continue;
			perror("vccd: kevent");
			return 1;
		}
		if (out.filter == EVFILT_SIGNAL) {
			print_counts();
			if (out.ident == SIGTERM)
				drain_exit(ls, sock);
			continue;
		}
#else
	/* Linux: epoll on the listening socket and a signalfd for USR1/TERM */
	{
		sigset_t ss;
		struct epoll_event e = { .events = EPOLLIN };
		int sfd;

		sigemptyset(&ss);
		sigaddset(&ss, SIGUSR1);
		sigaddset(&ss, SIGTERM);
		sfd = signalfd(-1, &ss, SFD_CLOEXEC);
		kq = epoll_create1(EPOLL_CLOEXEC);
		e.data.fd = ls;
		if (kq < 0 || sfd < 0 || epoll_ctl(kq, EPOLL_CTL_ADD, ls, &e) < 0) {
			perror("vccd: epoll");
			return 1;
		}
		e.data.fd = sfd;
		epoll_ctl(kq, EPOLL_CTL_ADD, sfd, &e);
		for (;;) {
			struct epoll_event out;
			int nev;

			park_accept();
			if ((nev = epoll_wait(kq, &out, 1, 50)) == 0)
				continue;
			if (nev < 0) {
				if (errno == EINTR)
					continue;
				perror("vccd: epoll_wait");
				return 1;
			}
			if (out.data.fd == sfd) {
				struct signalfd_siginfo si;

				if (read(sfd, &si, sizeof si) != sizeof si)
					continue;
				print_counts();
				if (si.ssi_signo == SIGTERM)
					drain_exit(ls, sock);
				continue;
			}
#endif
		for (;;) {
			int c;

			if (atomic_load(&gen_restart))
				break;	/* (parks at the loop's top) */
			c = accept(ls, NULL, NULL);

			if (c < 0)
				break;
			if (!peer_is_us(c)) {	/* another user: refused before reading anything */
				close(c);
				continue;
			}
			fcntl(c, F_SETFL, 0);	/* BSD: accept() inherits O_NONBLOCK */
			fcntl(c, F_SETFD, FD_CLOEXEC);
			{	/* the shim sends its request right after connecting: a client that
				   stays silent frees its worker instead of holding it */
				struct timeval tv = { 2, 0 };

				setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
			}
			queue_push(c);
		}
	}
#ifndef __APPLE__
	}
#endif
}
