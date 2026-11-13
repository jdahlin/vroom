/*
 * request.c: receive a request from a connection, validate it, split it into cwd, argv and
 * environment, and the small helpers that work on it (say, req_env, log_req).
 *
 * recv_req takes the header and the three fds (SCM_RIGHTS, close-on-exec) and then the payload into
 * the worker's buffer; split_req points into that buffer, so a struct req is valid only while the
 * buffer is: until the thread's next request.  Everything arrives from a client we do not trust
 * (the socket is 0600 and vccd checks the peer, but the bytes are still checked): the header is checked before it is used, the
 * counts in 64 bits, the payload's last byte must be a NUL so the strlen's stay inside it.  A request
 * that fails any check closes its fds and is dropped.  `say` writes a message to the client's
 * stderr in the form `TOOL: error: ...`.
 */
#include "vccd-internal.h"

VPRIV bool read_full(int fd, char *p, size_t n)
{
	while (n) {
		ssize_t r = read(fd, p, n);

		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return false;
		p += r;
		n -= r;
	}
	return true;
}

VPRIV bool write_full(int fd, const char *p, size_t n)
{
	while (n) {
		ssize_t r = write(fd, p, n);

		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return false;
		p += r;
		n -= r;
	}
	return true;
}


VPRIV bool recv_req(struct worker *w, int conn, struct req *r)
{
	struct vroom_req_hdr *h = (struct vroom_req_hdr *)w->raw;
	char cbuf[CMSG_SPACE(16 * sizeof(int))] = { 0 };
	struct iovec iov = { h, sizeof(*h) };
	int nfd = 0, bad = 0;
	struct msghdr m = { 0 };
	struct cmsghdr *c;

	m.msg_iov = &iov;
	m.msg_iovlen = 1;
	m.msg_control = cbuf;
	m.msg_controllen = sizeof(cbuf);
	r->fds[0] = r->fds[1] = r->fds[2] = -1;
	struct pollfd pf = { conn, POLLIN, 0 };
	/* the shim sends right after connecting: a silent client frees the worker
	   (macOS: SO_RCVTIMEO does not bound recvmsg with MSG_WAITALL) */
	ssize_t got = poll(&pf, 1, 2000) == 1 ? recvmsg(conn, &m, MSG_WAITALL) : -1;

	/* exactly three fds (stdin, stdout, stderr); any other count closes all we got */
	if (got < 0)
		m.msg_controllen = 0;
	for (c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c)) {
		if (c->cmsg_len < CMSG_LEN(0))	/* (CMSG_NXTHDR would not advance) */
			break;
		if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS)
			continue;
		size_t k = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);

		for (size_t i = 0; i < k; i++) {
			int fd;

			memcpy(&fd, CMSG_DATA(c) + i * sizeof(int), sizeof(int));
			fcntl(fd, F_SETFD, FD_CLOEXEC);
			if (nfd < 3)
				r->fds[nfd] = fd;
			else
				close(fd), bad = 1;
			nfd++;
		}
	}
	if (bad || nfd != 3 || (m.msg_flags & MSG_CTRUNC) || got != (ssize_t)sizeof(*h)) {
		for (int i = 0; i < 3; i++)
			if (r->fds[i] >= 0)
				close(r->fds[i]), r->fds[i] = -1;
		return false;
	}
	if (h->magic != VROOM_MAGIC || h->payload_len > VROOM_MAX_PAYLOAD || h->payload_len == 0 || h->pad ||
	    1 + (uint64_t)h->argc + h->envc > MAX_STRS)	/* (64 bits: argc 0xffffffff wraps to a small count) */
		return false;
	if (!read_full(conn, w->raw + sizeof(*h), h->payload_len))
		return false;
	if (w->raw[sizeof(*h) + h->payload_len - 1])
		return false;	/* the last string's NUL: split_req's strlen stays in the payload */
	return split_req(w->raw, w->strs, r);
}

/* the strings of a request in raw (header + payload): cwd, argv..., envp... */
VPRIV bool split_req(char *raw, char **strs, struct req *r)
{
	struct vroom_req_hdr *h = (struct vroom_req_hdr *)raw;
	size_t off, i, n;

	r->raw = raw;
	r->rawlen = sizeof(*h) + h->payload_len;

	/* split NUL-separated strings: cwd, argv..., envp... */
	n = 0;
	for (off = sizeof(*h); off < r->rawlen && n < MAX_STRS - 2; off += strlen(raw + off) + 1)
		strs[n++] = raw + off;
	if (n != 1 + h->argc + h->envc || h->argc < 1)
		return false;
	r->cwd = strs[0];
	r->argv = strs + 1;
	r->argc = h->argc;
	/* argv and envp each need a NULL terminator: shift envp up by one */
	for (i = n; i > 1 + h->argc; i--)
		strs[i] = strs[i - 1];
	strs[1 + h->argc] = NULL;
	r->envp = strs + 2 + h->argc;
	r->envp[h->envc] = NULL;
	r->tool = strrchr(r->argv[0], '/') ? strrchr(r->argv[0], '/') + 1 : r->argv[0];
	return true;
}

VPRIV void say(const struct req *r, const char *fmt, const char *arg)
{
	char line[512];
	int n = snprintf(line, sizeof(line), fmt, r->tool, arg);

	if (n > 0)
		(void)!write(r->fds[2], line, n < (int)sizeof(line) ? n : (int)sizeof(line) - 1);
}
VPRIV double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

VPRIV const char *req_env(const struct req *r, const char *name)
{
	size_t n = strlen(name);

	for (char **e = r->envp; *e; e++)
		if (!strncmp(*e, name, n) && (*e)[n] == '=')
			return *e + n + 1;
	return NULL;
}
VPRIV void log_req(const struct req *r, const char *kind, int status, double t0)
{
	if (log_requests) {
		double t1 = now_ms();

		/* @: start time (monotonic ms), for concurrency timelines; last: the input */
		fprintf(stderr, "vccd %-10s %7.2f ms rc=%d @%.2f %s %.200s\n", kind, t1 - t0, status, t0,
			kind[0] == 'b' ? "" : r->argc > 1 ? r->argv[1] : "",
			kind[0] == 'b' || r->argc < 2 ? "" : r->argv[r->argc - 1]);
		if (status && !strcmp(kind, "cc-host")) {	/* the whole command line of a failed host job */
			fputs("vccd   argv:", stderr);
			for (int i = 0; i < r->argc; i++)
				fprintf(stderr, " %s", r->argv[i]);
			fputc('\n', stderr);
		}
	}
}
