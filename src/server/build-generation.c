/*
 * build-generation.c: tell one make invocation from the next, flush the caches between them, and
 * restart the server when it has grown too large.
 *
 * The caches (files, directories, includes, diagnostics store) answer "what is at this path" for the
 * server's life, and files change between builds, so vccd_generation_enter runs at the start of every request
 * under vccd_generation_lock: a process group not seen lately is a new build (the details are below).  vccd_generation_leave
 * ends the request.  vccd_generation_enter returns 1 (answer VCCD_STATUS_RETRY: a restart is under way) or 2 (the
 * same, and this worker then calls vccd_generation_restart); the shim sends the request again.  The restart is
 * the only way to free memory, since every cache and the identifier table only grow: it happens at a
 * build boundary with no job running, or when the identifier table is full (every later TU would
 * fail).  The restart execs the server on the same listening socket, so clients wait in the backlog.
 * Everything here is called from worker threads (or shared-pool jobs) and the accept loop
 * (vccd_accept_park); the group table and vccd_generation_running_count are under vccd_generation_lock, the queue under vccd_queue_lock.
 */
#include "vccd-internal.h"

/*
 * Builds.  The caches answer "what is at this path" for the server's life;
 * between builds files change.  A request from a process group the server
 * has not seen lately is a new make invocation: if no job is running, the
 * file, directory and include caches are emptied first (old entries stay
 * allocated, so nothing in flight dangles).  If jobs are running the caches
 * stay and nothing waits, but a new cache epoch begins (vcache_new_epoch):
 * every cached file, directory and include answer is checked against the
 * file system once (a stat, no lock held) before the next lookup uses it,
 * and an answer that no longer holds is dropped and found again.  Two makes
 * through one server thus share the caches instead of draining each other,
 * and a file edited between them is seen.
 * A group stays known for VCCD_GENERATION_KEEP_MILLISECONDS after its last request, so a make with
 * gaps between its jobs does not look new when it comes back.
 */
#define VCCD_GENERATION_KEEP_MILLISECONDS 10000.0
#define VCCD_GENERATION_GROUPS_MAX 64
static pthread_mutex_t vccd_generation_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { uint32_t pgrp; double ms; } vccd_generation_groups[VCCD_GENERATION_GROUPS_MAX];
static int vccd_generation_group_count, vccd_generation_running_count;
_Atomic unsigned long vccd_flush_count;

int vccd_restart_argc, vccd_listen_fd = -1;
char **vccd_restart_argv;
const char *vccd_socket_path;
_Atomic int vccd_generation_restart_pending;

/* resident memory in MB */
static size_t vccd_generation_resident_megabytes(void)
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
 * request it holds or has queued is answered VCCD_STATUS_RETRY (the shim
 * sends it again), then it execs itself on the same listening socket.
 * vccd_generation_restart_is_due() decides (vccd_generation_enter, idle); the worker that saw it answers its
 * own request and calls vccd_generation_restart(), which returns only if exec fails.
 */
static bool vccd_generation_restart_is_due(void)
{
	const char *e = getenv("VCC_RESTART_MB");
	size_t lim = e ? strtoul(e, NULL, 10) : 6144;

	return vccd_listen_fd >= 0 && vccd_self_executable[0] && vccd_generation_resident_megabytes() >= lim;
}

/* the accept loop: during a restart it accepts nothing more (connections
   wait in the kernel's backlog for the new process) */
static _Atomic int vccd_accept_parked;

VPRIV void vccd_accept_park(void)
{
	if (!atomic_load(&vccd_generation_restart_pending))
		return;
	atomic_store(&vccd_accept_parked, 1);
	while (atomic_load(&vccd_generation_restart_pending))	/* (until the exec, or its failure) */
		usleep(1000);
	atomic_store(&vccd_accept_parked, 0);
}

VPRIV void vccd_generation_restart(void)
{
	while (!atomic_load(&vccd_accept_parked))
		usleep(1000);
	for (;;) {	/* the other workers answer what they took (vccd_active_count counts us too) */
		pthread_mutex_lock(&vccd_queue_lock);
		if (atomic_load(&vccd_active_count) == 1) {
			while (vccd_queue_head != vccd_queue_tail) {	/* still queued: never read */
				int c = vccd_queue_fds[vccd_queue_head++ % VCCD_QUEUE_SIZE];
				int32_t st = (int32_t)VCCD_STATUS_RETRY;

				(void)!write(c, &st, sizeof(st));
				close(c);
			}
			break;	/* (vccd_queue_lock stays held: nobody takes another) */
		}
		pthread_mutex_unlock(&vccd_queue_lock);
		usleep(1000);
	}
	{
		char fd[16], **av = calloc((size_t)vccd_restart_argc + 3, sizeof(char *));
		int k = 0;

		for (int i = 0; i < vccd_restart_argc; i++) {
			if (!strcmp(vccd_restart_argv[i], "--listen-fd")) {
				i++;
				continue;
			}
			av[k++] = vccd_restart_argv[i];
		}
		snprintf(fd, sizeof(fd), "%d", vccd_listen_fd);
		av[k++] = "--listen-fd", av[k++] = fd, av[k] = NULL;
		fprintf(stderr, "vccd: restarting (%zu MB resident; identifier table %s)\n", vccd_generation_resident_megabytes(),
			atomic_load(&vtables_full) ? "full" : "not full");
		vccd_counters_print();
		fcntl(vccd_listen_fd, F_SETFD, 0);
		fcntl(vccd_listen_fd, F_SETFL, 0);
		execv(vccd_self_executable, av);
		perror("vccd: restart");	/* go on with the old process */
		fcntl(vccd_listen_fd, F_SETFD, FD_CLOEXEC);
		fcntl(vccd_listen_fd, F_SETFL, O_NONBLOCK);
		atomic_store(&vccd_generation_restart_pending, 0);
		pthread_mutex_unlock(&vccd_queue_lock);
	}
}

/* 1: answer VCCD_STATUS_RETRY (the server is restarting); 2: the same, and
   then restart (vccd_generation_restart) */
VPRIV int vccd_generation_enter(uint32_t pgrp)
{
	VLOCK(vccd_generation_lock);
	if (atomic_load(&vccd_generation_restart_pending))
		return 1;
	if (atomic_load(&vtables_full) && !vccd_generation_running_count && vccd_listen_fd >= 0 && vccd_self_executable[0]) {
		/* identifiers are never freed: a full table fails every later TU */
		atomic_store(&vccd_generation_restart_pending, 1);
		return 2;
	}
	if (pgrp) {
		double t = vccd_clock_milliseconds();
		int i, k = 0;

		for (i = 0; i < vccd_generation_group_count; i++)
			if (vccd_generation_groups[i].pgrp == pgrp)
				break;
		if (i == vccd_generation_group_count) {	/* a new build */
			if (vccd_generation_group_count && !vccd_generation_running_count) {
				if (vccd_generation_restart_is_due()) {
					atomic_store(&vccd_generation_restart_pending, 1);
					return 2;
				}
				lex_cache_flush();
				pp_directory_cache_flush();
				vccd_diagnostics_build_new();
				pp_include_cache_flush();
				atomic_fetch_add(&vccd_flush_count, 1);
			} else if (vccd_generation_group_count) {	/* jobs are running: keep the caches, but check each answer once before it is reused */
				vcache_new_epoch();
			}
			for (int j = 0; j < vccd_generation_group_count; j++)	/* forget the groups long gone */
				if (t - vccd_generation_groups[j].ms < VCCD_GENERATION_KEEP_MILLISECONDS)
					vccd_generation_groups[k++] = vccd_generation_groups[j];
			vccd_generation_group_count = k < VCCD_GENERATION_GROUPS_MAX ? k : VCCD_GENERATION_GROUPS_MAX - 1;	/* (all live: the last one makes room) */
			i = vccd_generation_group_count++;
			vccd_generation_groups[i].pgrp = pgrp;
		}
		vccd_generation_groups[i].ms = t;
	}
	vccd_generation_running_count++;
	return 0;
}

VPRIV void vccd_generation_leave(void)
{
	VLOCK(vccd_generation_lock);
	vccd_generation_running_count--;
}	/* connections taken by a worker, not yet answered */
