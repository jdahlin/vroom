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
 * (driver_client_connection, the request buffers) is in thread-locals. */
#include "vccd-internal.h"

/* the request buffers of this thread: mmap'ed once, kept for its life */
static struct vccd_worker *vccd_worker_buffers_get(void)
{
	static _Thread_local struct vccd_worker w;

	if (!w.raw) {
		w.raw = mmap(NULL, sizeof(struct vccd_request_header) + VCCD_PROTOCOL_PAYLOAD_MAX,
			     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		w.strs = mmap(NULL, VCCD_STRINGS_MAX * sizeof(char *), PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_ANON, -1, 0);
		if (w.raw == MAP_FAILED || w.strs == MAP_FAILED) {
			perror("vccd: mmap");
			exit(1);
		}
	}
	return &w;
}

/* one connection, taken from the queue (counted in vccd_active_count): receive the
   request, run it, answer, close */
static void vccd_worker_connection_serve(struct vccd_worker *w, int conn)
{
	struct vccd_request r;
	int32_t status = 127;
	const char *kind = "bad";
	double t0 = vccd_clock_milliseconds();
	bool got;
	int i;

	vtask_blocking_begin();	/* a client that has not sent yet: up to 2 s */
	got = vccd_request_receive(w, conn, &r);
	vtask_blocking_end();
	if (got) {
		struct vccd_request_header *h = (struct vccd_request_header *)w->raw;

		if (h->build && vccd_self_build_id && h->build != vccd_self_build_id) {
			status = (int32_t)VCCD_STATUS_MISMATCH, kind = "mismatch";
		} else {
			/* a diagnostics query belongs to no build: it must not end the one it asks about */
			bool q = r.argc == 2 && !strncmp(r.argv[1], "--vccd-diagnostics", 18);
			int g;

			vtask_blocking_begin();	/* (waits while another build drains) */
			g = vccd_generation_enter(q ? 0 : h->pgrp);
			vtask_blocking_end();
			if (g) {
				status = (int32_t)VCCD_STATUS_RETRY, kind = "retry";
				if (g == 2) {	/* answer first: the exec drops every fd we hold */
					(void)vccd_fd_write_full(conn, (char *)&status, sizeof(status));
					close(conn);
					for (i = 0; i < 3; i++)
						if (r.fds[i] >= 0)
							close(r.fds[i]), r.fds[i] = -1;
					vccd_generation_restart();
					conn = open("/dev/null", O_RDWR);	/* (exec failed: the close below) */
				}
			} else {
				driver_client_connection = conn;
				status = vccd_request_handle(&r, &kind);
				driver_client_connection = -1;
				vccd_generation_leave();
			}
		}
	}
	vtask_blocking_begin();
	(void)vccd_fd_write_full(conn, (char *)&status, sizeof(status));
	close(conn);
	for (i = 0; i < 3; i++)
		if (r.fds[i] >= 0)
			close(r.fds[i]);
	vtask_blocking_end();
	vccd_request_log(&r, kind, status, t0);
	atomic_fetch_sub(&vccd_active_count, 1);
}

VPRIV void *vccd_worker_main(void *arg)
{
	struct vccd_worker *w = vccd_worker_buffers_get();

	(void)arg;
	for (;;)
		vccd_worker_connection_serve(w, vccd_queue_pop());	/* (counts itself in vccd_active_count) */
	return NULL;
}

/* --shared-pool: one job per accepted socket (spawned by the accept thread
   after vccd_queue_push); the socket waits in the queue, so a restart can still
   answer the ones no job has taken */
VPRIV void vccd_worker_pool_job(void *arg)
{
	int conn = vccd_queue_pop_try();

	(void)arg;
	if (conn >= 0)
		vccd_worker_connection_serve(vccd_worker_buffers_get(), conn);
}

/* SIGTERM: no new connections; the queued and running jobs finish, then exit */
VPRIV __attribute__((noreturn)) void vccd_server_drain_exit(int ls, const char *sock)
{
	close(ls);
	unlink(sock);
	for (;;) {
		bool idle;

		{
			VLOCK(vccd_queue_lock);
			idle = vccd_queue_head == vccd_queue_tail && !atomic_load(&vccd_active_count);
		}
		if (idle)
			exit(0);
		usleep(5000);
	}
}
