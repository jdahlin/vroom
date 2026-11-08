/* vroom-cc: tunn klient. Skickar argv/env/cwd + fd 0,1,2 till vroomd och
 * returnerar dess exitstatus. Faller tillbaka på riktig clang om servern saknas.
 *
 *   VROOM_SOCK      socketsökväg (default /tmp/vroomd.sock)
 *   VROOM_REAL_CC   kompilator vid fallback (default /opt/homebrew/opt/llvm/bin/clang)
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "protocol.h"

extern char **environ;

static void fallback(char **argv)
{
	const char *real = getenv("VROOM_REAL_CC");
	if (!real)
#ifdef VROOM_LLVM_BIN
		real = VROOM_LLVM_BIN "/clang";
#else
		real = "/opt/homebrew/opt/llvm/bin/clang";
#endif
	argv[0] = (char *)real;
	execv(real, argv);
	perror("vroom-cc: execv");
	_exit(127);
}

int main(int argc, char **argv)
{
	const char *path = getenv("VROOM_SOCK");
	if (!path)
		path = "/tmp/vroomd.sock";

	int s = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
	if (s < 0 || connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0)
		fallback(argv);

	char cwd[4096];
	if (!getcwd(cwd, sizeof(cwd)))
		fallback(argv);

	int envc = 0;
	size_t len = strlen(cwd) + 1;
	for (int i = 0; i < argc; i++)
		len += strlen(argv[i]) + 1;
	for (char **e = environ; *e; e++, envc++)
		len += strlen(*e) + 1;
	if (len > VROOM_MAX_PAYLOAD)
		fallback(argv);

	char *buf = malloc(sizeof(struct vroom_req_hdr) + len);
	struct vroom_req_hdr *h = (struct vroom_req_hdr *)buf;
	h->magic = VROOM_MAGIC;
	h->argc = argc;
	h->envc = envc;
	h->payload_len = len;
	char *p = buf + sizeof(*h);
	p = stpcpy(p, cwd) + 1;
	for (int i = 0; i < argc; i++)
		p = stpcpy(p, argv[i]) + 1;
	for (char **e = environ; *e; e++)
		p = stpcpy(p, *e) + 1;

	int fds[3] = { 0, 1, 2 };
	char cbuf[CMSG_SPACE(sizeof(fds))];
	memset(cbuf, 0, sizeof(cbuf));
	struct iovec iov = { .iov_base = buf, .iov_len = sizeof(*h) + len };
	struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1,
			    .msg_control = cbuf, .msg_controllen = sizeof(cbuf) };
	struct cmsghdr *c = CMSG_FIRSTHDR(&m);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(fds));
	memcpy(CMSG_DATA(c), fds, sizeof(fds));

	/* Första sendmsg bär fd:erna och början av datat; resten skickas med write. */
	ssize_t n = sendmsg(s, &m, 0);
	if (n < 0)
		fallback(argv);
	size_t off = n;
	while (off < iov.iov_len) {
		n = write(s, buf + off, iov.iov_len - off);
		if (n <= 0) {
			if (errno == EINTR)
				continue;
			fallback(argv);
		}
		off += n;
	}

	int32_t status;
	size_t got = 0;
	while (got < sizeof(status)) {
		n = read(s, (char *)&status + got, sizeof(status) - got);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			fprintf(stderr, "vroom-cc: servern stängde anslutningen\n");
			return 1;
		}
		got += n;
	}
	return status;
}
