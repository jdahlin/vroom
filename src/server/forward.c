/* Forwarding a request to another vroom-protocol server (the backend). */
#include "vccd-internal.h"

static int forward_relay(const struct req *r, const char *sock);

/* relay the request unchanged to another vroom-protocol server; the wait
   for its answer is a blocking region (the backend uses the core) */
VPRIV int forward(const struct req *r, const char *sock)
{
	int status;

	vtask_blocking_begin();
	status = forward_relay(r, sock);
	vtask_blocking_end();
	return status;
}

static int forward_relay(const struct req *r, const char *sock)
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

/* forward r with argv[0..argc) in place of its own (the same cwd, env and fds) */
VPRIV int forward_argv(const struct req *r, int argc, char **argv)
{
	const struct vroom_req_hdr *h = (const struct vroom_req_hdr *)r->raw;
	struct vroom_req_hdr *nh;
	struct req r2 = *r;
	size_t len = strlen(r->cwd) + 1, envc = 0;
	char *raw, *p;
	int st;

	for (int i = 0; i < argc; i++)
		len += strlen(argv[i]) + 1;
	for (char **e = r->envp; *e; e++, envc++)
		len += strlen(*e) + 1;
	if (len > VROOM_MAX_PAYLOAD || !(raw = malloc(sizeof(*nh) + len)))
		return 127;
	nh = (struct vroom_req_hdr *)raw;
	*nh = *h;
	nh->argc = (uint32_t)argc;
	nh->envc = (uint32_t)envc;
	nh->payload_len = (uint32_t)len;
	p = raw + sizeof(*nh);
	p = stpcpy(p, r->cwd) + 1;
	for (int i = 0; i < argc; i++)
		p = stpcpy(p, argv[i]) + 1;
	for (char **e = r->envp; *e; e++)
		p = stpcpy(p, *e) + 1;
	r2.raw = raw;
	r2.rawlen = sizeof(*nh) + len;
	st = forward(&r2, backend_sock);
	free(raw);
	return st;
}
