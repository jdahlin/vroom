/*
 * Leaving the job when a server-wide table or region is full: an error for
 * this job (and vccd restarts when idle: vtables_full), abort without a job
 * to end.  The job's hook is the driver's (an error for this TU, longjmp).
 */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "common/fail.h"

void (*vintern_full_hook)(void);
_Atomic int vtables_full;

_Thread_local int vjob_escape_fd = 2;

void vjob_escape(const char *msg)
{
	atomic_store(&vtables_full, 1);
	void (*hook)(void) = __atomic_load_n(&vintern_full_hook, __ATOMIC_RELAXED);

	dprintf(vjob_escape_fd, "vcc: error: %s\n", msg);
	if (vjob_escape_fd != 2)	/* (the server's log too) */
		fprintf(stderr, "vcc: error: %s\n", msg);
	if (hook)
		hook();
	abort();
}
