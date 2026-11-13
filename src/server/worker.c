/* The worker threads and the drain at exit. */
#include "vccd-internal.h"

VPRIV void *worker_main(void *arg)
{
	struct worker w;

	(void)arg;
	w.raw = mmap(NULL, sizeof(struct vroom_req_hdr) + VROOM_MAX_PAYLOAD,
		     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	w.strs = mmap(NULL, MAX_STRS * sizeof(char *), PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANON, -1, 0);
	if (w.raw == MAP_FAILED || w.strs == MAP_FAILED) {
		perror("vccd: mmap");
		exit(1);
	}
	for (;;) {
		int conn = queue_pop();	/* (counts itself in n_active) */
		struct req r;
		int32_t status = 127;
		const char *kind = "bad";
		double t0 = now_ms();
		int i;

		if (recv_req(&w, conn, &r)) {
			struct vroom_req_hdr *h = (struct vroom_req_hdr *)w.raw;

			if (h->build && self_build && h->build != self_build) {
				status = (int32_t)VROOM_STATUS_MISMATCH, kind = "mismatch";
			} else {
				/* a diagnostics query belongs to no build: it must not end the one it asks about */
				bool q = r.argc == 2 && !strncmp(r.argv[1], "--vccd-diagnostics", 18);
				int g = gen_enter(q ? 0 : h->pgrp);

				if (g) {
					status = (int32_t)VROOM_STATUS_RETRY, kind = "retry";
					if (g == 2) {	/* answer first: the exec drops every fd we hold */
						(void)write_full(conn, (char *)&status, sizeof(status));
						close(conn);
						for (i = 0; i < 3; i++)
							if (r.fds[i] >= 0)
								close(r.fds[i]), r.fds[i] = -1;
						do_restart();
						conn = open("/dev/null", O_RDWR);	/* (exec failed: the loop's close) */
					}
				} else {
					ppd_conn = conn;
					status = handle(&r, &kind);
					ppd_conn = -1;
					gen_leave();
				}
			}
		}
		(void)write_full(conn, (char *)&status, sizeof(status));
		close(conn);
		for (i = 0; i < 3; i++)
			if (r.fds[i] >= 0)
				close(r.fds[i]);
		log_req(&r, kind, status, t0);
		atomic_fetch_sub(&n_active, 1);
	}
	return NULL;
}

/* SIGTERM: no new connections; the queued and running jobs finish, then exit */
VPRIV __attribute__((noreturn)) void drain_exit(int ls, const char *sock)
{
	close(ls);
	unlink(sock);
	for (;;) {
		bool idle;

		{
			VLOCK(qlock);
			idle = qhead == qtail && !atomic_load(&n_active);
		}
		if (idle)
			exit(0);
		usleep(5000);
	}
}
