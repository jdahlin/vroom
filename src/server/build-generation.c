/* Build generations (the caches are flushed when a new make starts) and the memory restart of the server. */
#include "vccd-internal.h"

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
_Atomic unsigned long n_flush;

int g_argc, g_ls = -1;
char **g_argv;
const char *g_sock;
_Atomic int gen_restart;

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

VPRIV void park_accept(void)
{
	if (!atomic_load(&gen_restart))
		return;
	atomic_store(&accept_parked, 1);
	while (atomic_load(&gen_restart))	/* (until the exec, or its failure) */
		usleep(1000);
	atomic_store(&accept_parked, 0);
}

VPRIV void do_restart(void)
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
VPRIV int gen_enter(uint32_t pgrp)
{
	VLOCK(gen_mu);
	while (gen_flushing && !atomic_load(&gen_restart))
		pthread_cond_wait(&gen_cv, &gen_mu);
	if (atomic_load(&gen_restart)) {
		return 1;
	}
	if (atomic_load(&vtables_full) && !gen_running && g_ls >= 0 && self_exe[0]) {
		/* identifiers are never freed: a full table fails every later TU */
		atomic_store(&gen_restart, 1);
		pthread_cond_broadcast(&gen_cv);
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
				return 2;
			}
			vlex_cache_flush();
			vdir_flush();
			dstore_new_build();
			pp_include_flush();
			atomic_fetch_add(&n_flush, 1);
			gen_flushing = 0;
			pthread_cond_broadcast(&gen_cv);
		}
		gen_pgrp = pgrp;
	}
	gen_running++;
	return 0;
}

VPRIV void gen_leave(void)
{
	VLOCK(gen_mu);
	if (!--gen_running)
		pthread_cond_broadcast(&gen_cv);
}	/* connections taken by a worker, not yet answered */
