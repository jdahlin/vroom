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
#include "capabilities.h"
#include "driver/declaration-cache.h"
#include "frontend/pp/pp.h"
#include "frontend/lex/lex.h"
#include "common/intern.h"
#include "common/fail.h"
#include "frontend/pp/dircache.h"
#include "driver/driver.h"
#include "utils/fixdep.h"
#include "diagnostics-store.h"
#include "utils/ar.h"
#include "utils/shell.h"
#include "ld/ld.h"
#include "common/scoped-cleanup.h"
#include "port/task.h"

#define VCCD_STRINGS_MAX	(VCCD_PROTOCOL_PAYLOAD_MAX / 2)
#define VCCD_QUEUE_SIZE 4096

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
#define VCCD_WORKER_STACK_SIZE (2u << 20)
#endif
#endif
#ifndef VCCD_WORKER_STACK_SIZE
#define VCCD_WORKER_STACK_SIZE (16u << 20)
#endif

enum { VCCD_PROBE_NONE, VCCD_PROBE_CC, VCCD_PROBE_AS, VCCD_PROBE_SOURCE, VCCD_PROBE_LINK };

/* shared state */
extern const char *vccd_backend_socket;
extern const char *vccd_linker_backend;
extern bool vccd_log_requests, vccd_own_preprocessor, vccd_own_compiler;
extern char vccd_self_executable[4096];
extern uint64_t vccd_self_build_id;
extern char tool_dir[4096];
extern int queue[VCCD_QUEUE_SIZE];
extern unsigned vccd_queue_head, vccd_queue_tail;
extern pthread_mutex_t vccd_queue_lock;
extern _Atomic int vccd_active_count;
extern _Atomic unsigned long vccd_flush_count;
extern int vccd_restart_argc, vccd_listen_fd;
extern char **g_argv;
extern const char *vccd_socket_path;
extern _Atomic int vccd_generation_restart_pending;
extern atomic_ulong vccd_probe_count, vccd_probe_compile_count, vccd_forward_count, vccd_spawn_count, vccd_preprocess_count, n_cc, vccd_failure_count, vccd_fallback_count, vccd_archive_count, vccd_link_count, vccd_host_count,
	vccd_objcopy_count, vccd_recipe_count, vccd_recipe_declined_count;

/* functions that cross a file */
VPRIV const char *vccd_request_input_find(const struct req *r);
VPRIV bool vccd_request_is_compile(const struct req *r);
VPRIV bool vccd_request_is_preprocess(const struct req *r);
VPRIV int vccd_compile_in_process(const struct req *r, const char *src, size_t len, bool final);
VPRIV int vccd_probe_cc_answer(const struct req *r, int kind);
VPRIV int vccd_probe_kind_classify(const struct req *r);
VPRIV int vccd_probe_stdin_compile(const struct req *r, const char *src, size_t n);
VPRIV int delegate(const struct req *r, const char *prog, const char **kind);
VPRIV bool vccd_request_is_delegated_compile(const struct req *r, const char *skip);
VPRIV void vccd_generation_restart(void);
VPRIV __attribute__((noreturn)) void vccd_server_drain_exit(int ls, const char *sock);
VPRIV int vccd_compile_fixdep_after(const struct req *r, int status);
VPRIV int forward(const struct req *r, const char *sock);
VPRIV int vccd_backend_forward_argv(const struct req *r, int argc, char **argv);
VPRIV int vccd_generation_enter(uint32_t pgrp);
VPRIV void vccd_generation_leave(void);
VPRIV int handle(const struct req *r, const char **kind);
VPRIV int vccd_host_program_compile(const struct req *r);
VPRIV bool vccd_request_is_host_link(const struct req *r);
VPRIV int vccd_linker_run_in_process(const struct req *r, int argc, char **argv);
VPRIV bool vccd_probe_ld_is_query(const struct req *r);
VPRIV int vccd_probe_ld_answer(const struct req *r);
VPRIV int vccd_linker_run(const struct req *r);
VPRIV void vccd_request_log(const struct req *r, const char *kind, int status, double t0);
VPRIV double now_ms(void);
VPRIV int vccd_objcopy_run(const struct req *r, bool nm);
VPRIV void vccd_accept_park(void);
VPRIV void vccd_counters_print(void);
VPRIV int vccd_queue_pop(void);
VPRIV bool vccd_queue_push(int fd);
VPRIV int vccd_queue_pop_try(void);
VPRIV void vccd_worker_pool_job(void *arg);
VPRIV int vccd_fd_read_all(int fd, char **buf, size_t *len);
VPRIV bool vccd_fd_read_full(int fd, char *p, size_t n);
VPRIV bool vccd_request_receive(struct worker *w, int conn, struct req *r);
VPRIV const char *vccd_request_environment_get(const struct req *r, const char *name);
VPRIV int vccd_recipe_run(const struct req *r);
VPRIV void say(const struct req *r, const char *fmt, const char *arg);
VPRIV int spawn(const struct req *r, const char *prog);
VPRIV bool vccd_request_split(char *raw, char **strs, struct req *r);
VPRIV void *vccd_worker_main(void *arg);
VPRIV bool vccd_fd_write_full(int fd, const char *p, size_t n);

#endif
