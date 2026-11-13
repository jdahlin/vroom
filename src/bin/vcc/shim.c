/*
 * vcc/vas/vld shim: the program make actually runs.  It does as little as
 * possible: one connect(), one sendmsg() carrying argv/env/cwd and fds 0-2
 * (SCM_RIGHTS), then it blocks on a 4-byte exit status.  Which tool is meant
 * is taken from argv[0] by the server.
 *
 * No stdio, no malloc: the request is assembled in a static buffer (BSS
 * pages are only touched as far as the request reaches).
 *
 *   VCC_SOCK   server socket (default: protocol.h vroom_default_sock)
 *
 * Built with -DVSH it is vsh, a SHELL for make (installed as .../vsh/sh, so
 * make keeps its no-shell fast path for simple commands): `sh -c SCRIPT`
 * goes to vccd, which runs kbuild's object and archive recipes itself
 * (rsh.c); anything vccd declines, any other argv, and a missing vccd run
 * the real shell, $VSH_REAL (default /bin/sh), with the same arguments.
 */
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <spawn.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "protocol.h"

#ifdef MSG_NOSIGNAL
#define SHIM_NOSIG MSG_NOSIGNAL	/* Linux: no SIGPIPE from a send to a closed peer */
#else
#define SHIM_NOSIG 0		/* Darwin: SO_NOSIGPIPE on the socket */
#endif

extern char **environ;

static char buf[sizeof(struct vroom_req_hdr) + VROOM_MAX_PAYLOAD];

#ifdef VSH
static char **g_argv;

/* the real shell, in this process: nothing has been written or run yet */
static void real_shell(void)
{
	const char *sh = getenv("VSH_REAL");

	if (!sh)
		sh = "/bin/sh";
	g_argv[0] = (char *)sh;
	execv(sh, g_argv);
	(void)!write(2, "vsh: cannot run the real shell\n", 31);
	_exit(127);
}
#endif

static void die(const char *msg)
{
	(void)!write(2, msg, strlen(msg));
	_exit(127);
}

static char *put(char *p, const char *s)
{
	size_t n = strlen(s) + 1;

	if (p + n > buf + sizeof(buf))
		die("vcc: request too large\n");
	memcpy(p, s, n);
	return p + n;
}

/* the vccd next to this binary (vsh/sh: one directory up); 0 when found */
static int vccd_path(char *v, size_t cap)
{
	char exe[4096], rp[4096], *sl;
	uint32_t el = sizeof(exe);
	struct stat st;

#ifdef __APPLE__
	if (_NSGetExecutablePath(exe, &el) || !realpath(exe, rp) || !(sl = strrchr(rp, '/')))
		return -1;
#else
	(void)el, (void)exe;
	if (!realpath("/proc/self/exe", rp) || !(sl = strrchr(rp, '/')))
		return -1;
#endif
	*sl = 0;
	if ((size_t)snprintf(v, cap, "%s/vccd", rp) < cap && !stat(v, &st))
		return 0;
	if ((size_t)snprintf(v, cap, "%s/../vccd", rp) < cap && !stat(v, &st))
		return 0;
	return -1;
}

#ifndef VSH
/* no server on the default socket: start one (under a lock, detached, its
 * log in the socket's directory) and wait up to 5 s for it to listen */
static int autostart(const char *dir, const struct sockaddr_un *sa)
{
	char v[4200], lk[1200], lg[1200];
	char *av[4];
	int s = -1, lfd, nul, log;
	posix_spawn_file_actions_t fa;
	posix_spawnattr_t at;
	pid_t pid;

	if (getenv("VCC_NO_AUTOSTART") || vccd_path(v, sizeof(v)))
		return -1;
	snprintf(lk, sizeof(lk), "%s/lock", dir);
	snprintf(lg, sizeof(lg), "%s/vccd.log", dir);
	if ((lfd = open(lk, O_RDWR | O_CREAT | O_CLOEXEC, 0600)) < 0 || flock(lfd, LOCK_EX))
		return -1;
	for (int t = 0; t < 500; t++) {
		s = socket(AF_UNIX, SOCK_STREAM, 0);
		if (s >= 0 && !connect(s, (const struct sockaddr *)sa, sizeof(*sa)))
			break;	/* another client started it while we waited for the lock */
		if (s >= 0)
			close(s);
		s = -1;
		if (t == 0) {
			av[0] = v, av[1] = "--sock", av[2] = (char *)sa->sun_path, av[3] = NULL;
			nul = open("/dev/null", O_RDWR | O_CLOEXEC);
			log = open(lg, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
			posix_spawn_file_actions_init(&fa);
			posix_spawn_file_actions_adddup2(&fa, nul, 0);
			posix_spawn_file_actions_adddup2(&fa, nul, 1);
			posix_spawn_file_actions_adddup2(&fa, log >= 0 ? log : nul, 2);
			posix_spawnattr_init(&at);
			posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSID);
			if (posix_spawn(&pid, v, &fa, &at, av, environ))
				t = 500;
			posix_spawnattr_destroy(&at);
			posix_spawn_file_actions_destroy(&fa);
			close(nul);
			if (log >= 0)
				close(log);
		}
		usleep(10000);
	}
	close(lfd);	/* (releases the lock) */
	return s;
}
#endif

int main(int argc, char **argv)
{
	const char *path = getenv("VCC_SOCK");
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	struct vroom_req_hdr *h = (struct vroom_req_hdr *)buf;
	char *p = buf + sizeof(*h);
	int fds[3] = { 0, 1, 2 };
	char cbuf[CMSG_SPACE(sizeof(fds))];
	struct iovec iov;
	struct msghdr m = { 0 };
	struct cmsghdr *c;
	int32_t status;
	size_t off, got;
	ssize_t n;
	char **e;
	int s, i;

#ifdef VSH
	g_argv = argv;
	if (argc != 3 || (strcmp(argv[1], "-c") && strcmp(argv[1], "-ec")))
		real_shell();
#endif
	char ddir[1024], dsock[1100];

	int deflt = !path;

	if (deflt) {
		/* (VSH: never creates the directory, a missing server means the real shell) */
#ifdef VSH
		int mk = 0;
#else
		int mk = 1;
#endif
		if (vroom_default_sock(ddir, sizeof(ddir), dsock, sizeof(dsock), mk)) {
#ifdef VSH
			real_shell();
#endif
			die("vcc: no private vccd directory (set VCC_SOCK or start vccd)\n");
		}
		path = dsock;
	}
	strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0 || connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
#ifdef VSH
		real_shell();
#else
		if (s >= 0)
			close(s);
		if (!deflt || (s = autostart(ddir, &sa)) < 0)
			die("vcc: cannot connect to vccd (VCC_SOCK)\n");
#endif
	}

	if (!getcwd(p, VROOM_MAX_PAYLOAD))
		die("vcc: getcwd failed\n");
	p += strlen(p) + 1;
	for (i = 0; i < argc; i++)
		p = put(p, argv[i]);
	for (e = environ; *e; e++)
		p = put(p, *e);
	{	/* the client's umask, for files the server creates (last: it wins) */
		char um[16] = "VCC_UMASK=";
		mode_t m = umask(0);

		umask(m);
		um[10] = (char)('0' + (m >> 6 & 7)), um[11] = (char)('0' + (m >> 3 & 7)), um[12] = (char)('0' + (m & 7));
		p = put(p, um);
	}

	{	/* the vccd next to this binary gives the build id */
		char v[4200];

		if (!vccd_path(v, sizeof(v)))
			h->build = vroom_build_id(v);
	}
	h->pgrp = (uint32_t)getpgrp();
	h->magic = VROOM_MAGIC;
	h->argc = argc;
	h->envc = e - environ + 1;
	h->payload_len = p - (buf + sizeof(*h));

	/* VROOM_STATUS_RETRY: the server restarts itself (memory) and has not run
	   the request; it listens again on the same socket in a moment */
	for (int tries = 0;; tries++) {
		memset(cbuf, 0, sizeof(cbuf));
		iov.iov_base = buf;
		iov.iov_len = p - buf;
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		m.msg_control = cbuf;
		m.msg_controllen = sizeof(cbuf);
		c = CMSG_FIRSTHDR(&m);
		c->cmsg_level = SOL_SOCKET;
		c->cmsg_type = SCM_RIGHTS;
		c->cmsg_len = CMSG_LEN(sizeof(fds));
		memcpy(CMSG_DATA(c), fds, sizeof(fds));

		/*
		 * The first sendmsg carries the fds; any remainder goes with send.
		 * A server that restarts answers RETRY and closes without reading
		 * the rest: the send then fails with EPIPE (never SIGPIPE: that
		 * would kill the client before it reads the RETRY), and the status
		 * is read all the same.
		 */
#ifdef SO_NOSIGPIPE
		{
			int one = 1;

			(void)setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
		}
#endif
		do
			n = sendmsg(s, &m, SHIM_NOSIG);
		while (n < 0 && errno == EINTR);
		if (n < 0 && errno != EPIPE && errno != ECONNRESET)
			die("vcc: sendmsg failed\n");
		for (off = n < 0 ? iov.iov_len : (size_t)n; off < iov.iov_len; off += n) {
			n = send(s, buf + off, iov.iov_len - off, SHIM_NOSIG);
			if (n < 0 && errno == EINTR) {
				n = 0;
				continue;
			}
			if (n < 0 && (errno == EPIPE || errno == ECONNRESET))
				break;	/* the status says whether to retry */
			if (n <= 0)
				die("vcc: write failed\n");
		}

		for (got = 0; got < sizeof(status); got += n) {
			n = read(s, (char *)&status + got, sizeof(status) - got);
			if (n < 0 && errno == EINTR) {
				n = 0;
				continue;
			}
			if (n <= 0)
				die("vcc: server closed the connection\n");
		}
		if (status != (int32_t)VROOM_STATUS_RETRY || tries == 20)
			break;
		close(s);
		for (int t = 0; t < 500; t++) {
			s = socket(AF_UNIX, SOCK_STREAM, 0);
			if (s >= 0 && !connect(s, (struct sockaddr *)&sa, sizeof(sa)))
				break;
			if (s >= 0)
				close(s);
			s = -1;
			usleep(10000);
		}
		if (s < 0)
			die("vcc: vccd did not come back after restarting\n");
	}
	if (status == (int32_t)VROOM_STATUS_MISMATCH)
		die("vcc: vccd runs another build of vcc than this shim; restart it\n");
#ifdef VSH
	if (status == (int32_t)VROOM_STATUS_DECLINE) {
		close(s);
		real_shell();
	}
#endif
	return status;
}
