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
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../protocol.h"

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

	if (!path) {
		if (vroom_default_sock(ddir, sizeof(ddir), dsock, sizeof(dsock), 0)) {
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
#endif
		die("vcc: cannot connect to vccd (VCC_SOCK)\n");
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

	h->magic = VROOM_MAGIC;
	h->argc = argc;
	h->envc = e - environ + 1;
	h->payload_len = p - (buf + sizeof(*h));

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

	/* the first sendmsg carries the fds; any remainder goes with write */
	do
		n = sendmsg(s, &m, 0);
	while (n < 0 && errno == EINTR);
	if (n < 0)
		die("vcc: sendmsg failed\n");
	for (off = n; off < iov.iov_len; off += n) {
		n = write(s, buf + off, iov.iov_len - off);
		if (n < 0 && errno == EINTR) {
			n = 0;
			continue;
		}
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
#ifdef VSH
	if (status == (int32_t)VROOM_STATUS_DECLINE) {
		close(s);
		real_shell();
	}
#endif
	return status;
}
