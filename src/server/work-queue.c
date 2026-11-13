/*
 * work-queue.c: the fixed-size queue of accepted sockets waiting for a worker thread (or, with
 * --shared-pool, for a pool job to take them).
 *
 * The accept loop pushes, workers pop; qlock guards the ring (qhead and qtail grow without bound and
 * are used modulo QSIZE).  n_active counts connections taken and not yet answered; it is raised
 * under qlock when a socket leaves the queue, so the restart and the exit drain (which wait for it to
 * reach zero or one) never see a socket in neither place.  A full queue closes the connection: the
 * shim reports an error.
 */
#include "vccd-internal.h"

int queue[QSIZE];
unsigned qhead, qtail;
pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qcond = PTHREAD_COND_INITIALIZER;

/* false: the queue is full and fd was closed (the shim reports an error) */
VPRIV bool queue_push(int fd)
{
	{
		VLOCK(qlock);
		if (qtail - qhead < QSIZE) {
			queue[qtail++ % QSIZE] = fd;
			pthread_cond_signal(&qcond);
			return true;
		}
	}
	close(fd);
	return false;
}
_Atomic int n_active;
VPRIV int queue_pop(void)
{
	int fd;

	VLOCK(qlock);
	while (qhead == qtail)
		pthread_cond_wait(&qcond, &qlock);
	fd = queue[qhead++ % QSIZE];
	atomic_fetch_add(&n_active, 1);	/* under the lock: drain_exit never sees it in neither place */
	return fd;
}

/* the shared pool's jobs: one socket each, or -1 when a restart already
   answered the queued ones */
VPRIV int queue_try_pop(void)
{
	VLOCK(qlock);
	if (qhead == qtail)
		return -1;
	atomic_fetch_add(&n_active, 1);
	return queue[qhead++ % QSIZE];
}
