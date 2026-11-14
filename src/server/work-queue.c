/*
 * work-queue.c: the fixed-size queue of accepted sockets waiting for a worker thread (or, with
 * --shared-pool, for a pool job to take them).
 *
 * The accept loop pushes, workers pop; vccd_queue_lock guards the ring (vccd_queue_head and vccd_queue_tail grow without bound and
 * are used modulo VCCD_QUEUE_SIZE).  vccd_active_count counts connections taken and not yet answered; it is raised
 * under vccd_queue_lock when a socket leaves the queue, so the restart and the exit drain (which wait for it to
 * reach zero or one) never see a socket in neither place.  A full queue closes the connection: the
 * shim reports an error.
 */
#include "vccd-internal.h"

int vccd_queue_fds[VCCD_QUEUE_SIZE];
unsigned vccd_queue_head, vccd_queue_tail;
pthread_mutex_t vccd_queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t vccd_queue_condition = PTHREAD_COND_INITIALIZER;

/* false: the queue is full and fd was closed (the shim reports an error) */
VPRIV bool vccd_queue_push(int fd)
{
	{
		VLOCK(vccd_queue_lock);
		if (vccd_queue_tail - vccd_queue_head < VCCD_QUEUE_SIZE) {
			vccd_queue_fds[vccd_queue_tail++ % VCCD_QUEUE_SIZE] = fd;
			pthread_cond_signal(&vccd_queue_condition);
			return true;
		}
	}
	close(fd);
	return false;
}
_Atomic int vccd_active_count;
VPRIV int vccd_queue_pop(void)
{
	int fd;

	VLOCK(vccd_queue_lock);
	while (vccd_queue_head == vccd_queue_tail)
		pthread_cond_wait(&vccd_queue_condition, &vccd_queue_lock);
	fd = vccd_queue_fds[vccd_queue_head++ % VCCD_QUEUE_SIZE];
	atomic_fetch_add(&vccd_active_count, 1);	/* under the lock: vccd_server_drain_exit never sees it in neither place */
	return fd;
}

/* the shared pool's jobs: one socket each, or -1 when a restart already
   answered the queued ones */
VPRIV int vccd_queue_pop_try(void)
{
	VLOCK(vccd_queue_lock);
	if (vccd_queue_head == vccd_queue_tail)
		return -1;
	atomic_fetch_add(&vccd_active_count, 1);
	return vccd_queue_fds[vccd_queue_head++ % VCCD_QUEUE_SIZE];
}
