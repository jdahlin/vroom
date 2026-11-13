/* What runs one accepted connection (receive, run, answer), the threads that
   run them, and the drain at exit.
 *
 * Two ways to run connections (vccd.c chooses at start):
 *   - worker threads (the default): -j N of them (2 x online CPUs), each
 *     taking sockets from the queue and doing everything itself, waits
 *     included;
 *   - --shared-pool / VCCD_SHARED_POOL=1: the accept thread spawns one job of
 *     the shared pool (port/task.c) per socket; at most the budget (-j,
 *     VCC_JOBS, else the granted cores) run CPU work at once, and every wait
 *     of a job (the request from a silent client, the reply, a generation
 *     drain, a child, the backend, a link lock) is a blocking region, so the
 *     core goes to the next queued job meanwhile
 *     (doc/quality/WORK-scheduler.md 1.4).
 * Either way a connection is served start to end on one thread: its context
 * (ppd_conn, the request buffers) is in thread-locals. */
#include "vccd-internal.h"

/* the request buffers of this thread: mmap'ed once, kept for its life */
static struct worker *worker_buffers(void)
{
	static _Thread_local struct worker w;

	if (!w.raw) {
		w.raw = mmap(NULL, sizeof(struct vroom_req_hdr) + VROOM_MAX_PAYLOAD,
			     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		w.strs = mmap(NULL, MAX_STRS * sizeof(char *), PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_ANON, -1, 0);
		if (w.raw == MAP_FAILED || w.strs == MAP_FAILED) {
			perror("vccd: mmap");
			exit(1);
		}
	}
	return &w;
}

/* one connection, taken from the queue (counted in n_active): receive the
   request, run it, answer, close */
static void serve(struct worker *w, int conn)
{
	struct req r;
	int32_t status = 127;
	const char *kind = "bad";
	double t0 = now_ms();
	bool got;
	int i;

	vtask_blocking_begin();	/* a client that has not sent yet: up to 2 s */
	got = recv_req(w, conn, &r);
	vtask_blocking_end();
	if (got) {
		struct vroom_req_hdr *h = (struct vroom_req_hdr *)w->raw;

		if (h->build && self_build && h->build != self_build) {
			status = (int32_t)VROOM_STATUS_MISMATCH, kind = "mismatch";
		} else {
			/* a diagnostics query belongs to no build: it must not end the one it asks about */
			bool q = r.argc == 2 && !strncmp(r.argv[1], "--vccd-diagnostics", 18);
			int g;

			vtask_blocking_begin();	/* (waits while another build drains) */
			g = gen_enter(q ? 0 : h->pgrp);
			vtask_blocking_end();
			if (g) {
				status = (int32_t)VROOM_STATUS_RETRY, kind = "retry";
				if (g == 2) {	/* answer first: the exec drops every fd we hold */
					(void)write_full(conn, (char *)&status, sizeof(status));
					close(conn);
					for (i = 0; i < 3; i++)
						if (r.fds[i] >= 0)
							close(r.fds[i]), r.fds[i] = -1;
					do_restart();
					conn = open("/dev/null", O_RDWR);	/* (exec failed: the close below) */
				}
			} else {
				ppd_conn = conn;
				status = handle(&r, &kind);
				ppd_conn = -1;
				gen_leave();
			}
		}
	}
	vtask_blocking_begin();
	(void)write_full(conn, (char *)&status, sizeof(status));
	close(conn);
	for (i = 0; i < 3; i++)
		if (r.fds[i] >= 0)
			close(r.fds[i]);
	vtask_blocking_end();
	log_req(&r, kind, status, t0);
	atomic_fetch_sub(&n_active, 1);
}

VPRIV void *worker_main(void *arg)
{
	struct worker *w = worker_buffers();

	(void)arg;
	for (;;)
		serve(w, queue_pop());	/* (counts itself in n_active) */
	return NULL;
}

/* --shared-pool: one job per accepted socket (spawned by the accept thread
   after queue_push); the socket waits in the queue, so a restart can still
   answer the ones no job has taken */
VPRIV void pool_job(void *arg)
{
	int conn = queue_try_pop();

	(void)arg;
	if (conn >= 0)
		serve(worker_buffers(), conn);
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
