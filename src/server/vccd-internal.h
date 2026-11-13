/*
 * vccd-internal.h: what the files of the server share: the request and worker
 * types, the state they keep between them (defined in the file named beside
 * each declaration) and the functions that cross a file.
 */
#ifndef VCCD_INTERNAL_H
#define VCCD_INTERNAL_H

/* functions one file defines and another calls: static when the server is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

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
#include "protocol.h"
#include "caps.h"
#include "driver/ccsnap.h"
#include "frontend/pp/pp.h"
#include "frontend/lex/lex.h"
#include "common/intern.h"
#include "common/fail.h"
#include "frontend/pp/dircache.h"
#include "driver/ppdriver.h"
#include "utils/fixdep.h"
#include "dstore.h"
#include "utils/ar.h"
#include "utils/rsh.h"
#include "ld/ld.h"
#include "common/scoped-cleanup.h"
#include "port/task.h"

#define MAX_STRS	(VROOM_MAX_PAYLOAD / 2)
#define QSIZE 4096

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

/* compiles recurse (parser, emitter): vstack_guard_low() ends a TU that nests too
   deeply.  TSan keeps a fixed shadow call stack (64K frames) per thread and
   corrupts its state past it, so under TSan the guard must fire sooner. */
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define WORKER_STACK (2u << 20)
#endif
#endif
#ifndef WORKER_STACK
#define WORKER_STACK (16u << 20)
#endif

enum { PROBE_NONE, PROBE_CC, PROBE_AS, PROBE_SRC, PROBE_LINK };

/* shared state */
extern const char *backend_sock;
extern const char *ld_backend;
extern bool log_requests, own_pp, own_cc;
extern char self_exe[4096];
extern uint64_t self_build;
extern char tool_dir[4096];
extern int queue[QSIZE];
extern unsigned qhead, qtail;
extern pthread_mutex_t qlock;
extern _Atomic int n_active;
extern _Atomic unsigned long n_flush;
extern int g_argc, g_ls;
extern char **g_argv;
extern const char *g_sock;
extern _Atomic int gen_restart;
extern atomic_ulong n_probe, n_probe_cc, n_fwd, n_spawn, n_pp, n_cc, n_fail, n_fallback, n_ar, n_ld, n_host,
	n_objcopy, n_sh, n_sh_decl;

/* functions that cross a file */
VPRIV const char *cc_input(const struct req *r);
VPRIV bool cc_is_compile(const struct req *r);
VPRIV bool cc_is_pp(const struct req *r);
VPRIV int cc_own(const struct req *r, const char *src, size_t len, bool final);
VPRIV int cc_probe(const struct req *r, int kind);
VPRIV int cc_probe_kind(const struct req *r);
VPRIV int compile_stdin(const struct req *r, const char *src, size_t n);
VPRIV int delegate(const struct req *r, const char *prog, const char **kind);
VPRIV bool dg_is_compile(const struct req *r, const char *skip);
VPRIV void do_restart(void);
VPRIV __attribute__((noreturn)) void drain_exit(int ls, const char *sock);
VPRIV int fixdep_after(const struct req *r, int status);
VPRIV int forward(const struct req *r, const char *sock);
VPRIV int forward_argv(const struct req *r, int argc, char **argv);
VPRIV int gen_enter(uint32_t pgrp);
VPRIV void gen_leave(void);
VPRIV int handle(const struct req *r, const char **kind);
VPRIV int host_cc(const struct req *r);
VPRIV bool host_cc_link(const struct req *r);
VPRIV int ld_in_process(const struct req *r, int argc, char **argv);
VPRIV bool ld_is_probe(const struct req *r);
VPRIV int ld_probe(const struct req *r);
VPRIV int ld_run(const struct req *r);
VPRIV void log_req(const struct req *r, const char *kind, int status, double t0);
VPRIV double now_ms(void);
VPRIV int objcopy_run_req(const struct req *r, bool nm);
VPRIV void park_accept(void);
VPRIV void print_counts(void);
VPRIV int queue_pop(void);
VPRIV bool queue_push(int fd);
VPRIV int queue_try_pop(void);
VPRIV void pool_job(void *arg);
VPRIV int read_all(int fd, char **buf, size_t *len);
VPRIV bool read_full(int fd, char *p, size_t n);
VPRIV bool recv_req(struct worker *w, int conn, struct req *r);
VPRIV const char *req_env(const struct req *r, const char *name);
VPRIV int run_sh(const struct req *r);
VPRIV void say(const struct req *r, const char *fmt, const char *arg);
VPRIV int spawn(const struct req *r, const char *prog);
VPRIV bool split_req(char *raw, char **strs, struct req *r);
VPRIV void *worker_main(void *arg);
VPRIV bool write_full(int fd, const char *p, size_t n);

#endif
