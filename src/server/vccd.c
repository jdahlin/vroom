/*
 * vccd: the vcc server.
 *
 * One process.  The main thread waits in kqueue (epoll on Linux) for connections from the
 * shims (vcc, vld) and hands each accepted socket to a worker thread, or with
 * --shared-pool to a job of the shared pool (worker.c says how the two
 * differ).  The thread receives the request (argv, env, cwd, fds 0-2) into a
 * per-thread buffer, then
 *
 *   - answers toolchain probes from the capability table in caps.c,
 *     without running anything: cc-option/ld-option, as-instr (the source
 *     on stdin is looked up), user-space link tests, --version,
 *     -print-file-name, and links with options vld refuses;
 *   - runs preprocessing jobs (-E: kbuild's linker scripts and device
 *     trees) itself, with vcc's preprocessor (driver/job.c), in this thread;
 *   - compiles -c/-S of .c (frontend/pp/ + frontend/c/ + codegen/) and .S (frontend/pp/ + as/as.c) itself,
 *     in this thread, into the .o and dependency file kbuild names (the
 *     vDSO's -mcmodel=tiny and stdin input included); the header cache is
 *     shared by all jobs, everything else is per thread.  What vcc fails on
 *     fails, with vcc's diagnostics (with --backend-sock: goes to clang);
 *   - links with vld (ld/ld.c) in this thread: vmlinux.o, vmlinux and the
 *     vDSO, with --vroom-stubs for the -O0 dead branches (with --ld PROG:
 *     PROG is run instead);
 *   - compiles and links the host programs (no --target) for macOS the same
 *     way: driver/job.c without a target writes Mach-O objects, vld's Mach-O
 *     path (platform/darwin/ldmacho.c) links them against libSystem.  No
 *     clang, no ld64 (with --backend-sock: they are forwarded there);
 *   - runs the archiver (ar, var, llvm-ar by argv[0]) itself: utils/ar.c;
 *   - runs nm and objcopy (vcc-nm, vcc-objcopy) itself: ld/objcopy.c;
 *   - with VCC_DELEGATE=PROG in the request's environment (a build that is
 *     not the kernel, e.g. PostgreSQL on macOS: bench/pg/build.sh vccd),
 *     a vcc request is either -c of a .c file, compiled here, or run as
 *     PROG with the same arguments (probes, links, --version, and any TU
 *     vcc fails on); sources containing $VCC_DELEGATE_SKIP go to PROG too;
 *   - runs make's recipes for vsh (argv[0] sh): utils/rsh.c interprets kbuild's
 *     subset of sh and runs the tools in it as requests here; anything else
 *     is answered VROOM_STATUS_DECLINE and vsh runs the real shell.
 *
 * No allocation per request: buffers are mmap'ed once per worker.
 *
 *   vccd [--sock PATH] [--backend-sock PATH] [--ld PATH] [-j N] [--shared-pool]
 *   -j N            worker threads (default 2 x online CPUs); with --shared-pool
 *                   the budget: compile jobs that run at once (default
 *                   VCC_JOBS, else the cores the process is granted)
 *   --shared-pool   (or VCCD_SHARED_POOL=1 at start) connections run as jobs
 *                   of the shared pool, links and emit helpers from the same
 *                   budget; waits give the core to the next job
 *                   (doc/quality/WORK-scheduler.md, stage 3)
 *   --backend-sock  a vroomd (clang) for host programs and for TUs vcc fails on
 *   --ld PROG       links run PROG (e.g. vroom-ld) instead of vld in process
 *   VCC_LOG=1   log one line per request to stderr (kind, ms, argv[0..2])
 *   VCC_PP=0    send -E jobs to the backend too (with --backend-sock)
 *   VCC_CC=0    send -c jobs to the backend too (with --backend-sock)
 * On SIGUSR1 (and at exit) the counts are printed: own compiles, failures.
 */

#include "vccd-internal.h"

const char *backend_sock;	/* vroomd (clang), only if given */
const char *ld_backend;		/* links run this (vroom-ld), only if given */
bool log_requests, own_pp = true, own_cc = true;
char self_exe[4096];	/* our own binary (realpath), for a restart */
uint64_t self_build;	/* vroom_build_id of our own binary */
char tool_dir[4096];	/* where vccd and its shims live (realpath) */
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
	 * two per core leaves room for the ones that block (recipes waiting on
	 * their tools, spawned tools) without a thread per possible connection */
	int nworkers = 2 * (int)sysconf(_SC_NPROCESSORS_ONLN), ls, kq, i, listen_fd = -1, jobs = 0;
	bool shared_pool = getenv("VCCD_SHARED_POOL") && !strcmp(getenv("VCCD_SHARED_POOL"), "1");

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
			ld_backend = argv[++i];	/* (old name of --ld) */
		else if (!strcmp(argv[i], "-j") && i + 1 < argc)
			nworkers = jobs = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--shared-pool"))
			shared_pool = true;
		else {
			fprintf(stderr, "usage: vccd [--sock P] [--backend-sock P] [--ld P] [-j N] [--shared-pool]\n");
			return 2;
		}
	}
	/* the shared pool's threads (emit helpers, link phases) run compiler code
	   as the workers do: the same stack, so input nests as deep on either */
	vtask_stack_size_set(WORKER_STACK);
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
	/* without a backend vcc does all of it */
	own_pp = !backend_sock || !getenv("VCC_PP") || strcmp(getenv("VCC_PP"), "0");
	own_cc = !backend_sock || !getenv("VCC_CC") || strcmp(getenv("VCC_CC"), "0");
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

	if (shared_pool) {
		vtask_budget_set(jobs > 0 ? jobs : vtask_jobs_from_environment(0));
		if (log_requests)
			fprintf(stderr, "vccd: shared pool, budget %d\n", vtask_budget());
	}
	for (i = 0; i < (shared_pool ? 0 : nworkers); i++) {
		pthread_t t;
		pthread_attr_t at;

		pthread_attr_init(&at);
		pthread_attr_setstacksize(&at, WORKER_STACK);
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
			if (queue_push(c) && shared_pool)
				vtask_spawn(pool_job, NULL);
		}
	}
#ifndef __APPLE__
	}
#endif
}
