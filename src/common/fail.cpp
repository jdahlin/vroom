/*
 * How a job, or a tool, stops (common/fail.h): the message, then the job's
 * jump buffer or exit.  The only abort() and exit() of the library.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/fail.h"

_Thread_local jmp_buf *vjob_jb;
_Thread_local int vjob_fail_fd = 2;
_Atomic int vtables_full;

void vjob_fail(const char *message)
{
	dprintf(vjob_fail_fd, "vcc: error: %s\n", message);
	if (vjob_fail_fd != 2)	/* (the server's log too) */
		fprintf(stderr, "vcc: error: %s\n", message);
	if (vjob_jb)
		longjmp(*vjob_jb, 1);
	exit(1);
}

void vjob_fail_errno(const char *what)
{
	char message[256];

	snprintf(message, sizeof(message), "%s: %s", what, strerror(errno));
	vjob_fail(message);
}

void vjob_fail_table_full(const char *message)
{
	atomic_store(&vtables_full, 1);
	vjob_fail(message);
}

void vout_of_memory(void)
{
	vjob_fail("out of memory");
}

void vbug(const char *file, int line, const char *message)
{
	fprintf(stderr, "vcc: internal error: %s:%d: %s\n", file, line, message);
	abort();
}
