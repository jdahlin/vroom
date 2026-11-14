/*
 * forward.c: pass a request on, unchanged, to another vroom-protocol server (the backend given by
 * --backend-sock: clang's vroomd).
 *
 * The request is sent as it came (header, payload, the client's three fds as SCM_RIGHTS) and the
 * backend's status word is the answer, so the backend runs the job with the client's own cwd,
 * environment and stdio.  vccd_backend_forward_argv does the same with another argv (the host-program link, the
 * stdin fallback).  Failure to connect is exit status 127 with a message.  The wait for the backend
 * is a blocking region (vtask_blocking_begin) so that, with --shared-pool, the core goes to the next
 * job meanwhile.
 */
#include "vccd-internal.h"

static int vccd_backend_relay(const struct vccd_request *r, const char *sock);

/* relay the request unchanged to another vroom-protocol server; the wait
   for its answer is a blocking region (the backend uses the core) */
VPRIV int vccd_backend_forward(const struct vccd_request *r, const char *sock)
{
	int status;

	vtask_blocking_begin();
	status = vccd_backend_relay(r, sock);
	vtask_blocking_end();
	return status;
}

static int vccd_backend_relay(const struct vccd_request *r, const char *sock)
{
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	char cbuf[CMSG_SPACE(3 * sizeof(int))];
	struct iovec iov = { r->raw, r->raw_length };
	struct msghdr m = { 0 };
	struct cmsghdr *c;
	int32_t status;
	ssize_t n;
	int s;

	strncpy(sa.sun_path, sock, sizeof(sa.sun_path) - 1);
	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0 || connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		vccd_request_message_write(r, "%s: error: backend %s not reachable\n", sock);
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
	if (n < 0 || !vccd_fd_write_full(s, r->raw + n, r->raw_length - n) ||
	    !vccd_fd_read_full(s, (char *)&status, sizeof(status)))
		status = 127;
	close(s);
	return status;
}

/* forward r with argv[0..argc) in place of its own (the same cwd, env and fds) */
VPRIV int vccd_backend_forward_argv(const struct vccd_request *r, int argc, char **argv)
{
	const struct vccd_request_header *h = (const struct vccd_request_header *)r->raw;
	struct vccd_request_header *nh;
	struct vccd_request r2 = *r;
	size_t len = strlen(r->cwd) + 1, envc = 0;
	char *raw, *p;
	int st;

	for (int i = 0; i < argc; i++)
		len += strlen(argv[i]) + 1;
	for (char **e = r->envp; *e; e++, envc++)
		len += strlen(*e) + 1;
	if (len > VCCD_PROTOCOL_PAYLOAD_MAX || !(raw = malloc(sizeof(*nh) + len)))
		return 127;
	nh = (struct vccd_request_header *)raw;
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
	r2.raw_length = sizeof(*nh) + len;
	st = vccd_backend_forward(&r2, vccd_backend_socket);
	free(raw);
	return st;
}
