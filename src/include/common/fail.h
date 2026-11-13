/*
 * The one way the library and its callers stop.
 *
 *   vjob_fail(message)   bad input, a limit or out of memory: the job ends with a
 *                        diagnostic.  The message goes to vjob_fail_fd (the job's
 *                        diagnostics; vccd: the client's stderr, and the server's
 *                        log too), then control leaves through the job's jump
 *                        buffer vjob_jb (the driver sets it around a translation
 *                        unit).  Without a job (a standalone tool, a thread that
 *                        has none) the process exits with status 1.
 *   vjob_fail_errno(w)   the same with "w: strerror(errno)" (a failed mmap)
 *   vjob_fail_table_full(message)
 *                        the same, for a server-wide table or region that is full:
 *                        also sets vtables_full, so that vccd restarts when idle
 *   vout_of_memory()     vjob_fail("out of memory")
 *   VBUG(message)        an internal inconsistency (a bug in vcc, not in the
 *                        input): prints the place and aborts
 *
 * Nothing else in the library calls abort() or exit(), or prints an error.
 * A scope that holds a VAUTO* or VLOCK resource must not call anything that
 * can vjob_fail (scoped-cleanup.h).
 */
#ifndef VCC_COMMON_FAIL_H
#define VCC_COMMON_FAIL_H

#include <setjmp.h>
#include <stdatomic.h>

extern _Thread_local jmp_buf *vjob_jb;	/* the job's exit, NULL outside a job */
extern _Thread_local int vjob_fail_fd;	/* where vjob_fail reports: the job's diagnostics (2) */
extern _Atomic int vtables_full;	/* set once a server-wide table was found full (vccd restarts) */

__attribute__((noreturn, cold)) void vjob_fail(const char *message);
__attribute__((noreturn, cold)) void vjob_fail_errno(const char *what);
__attribute__((noreturn, cold)) void vjob_fail_table_full(const char *message);
__attribute__((noreturn, cold)) void vout_of_memory(void);
__attribute__((noreturn, cold)) void vbug(const char *file, int line, const char *message);
#define VBUG(message) vbug(__FILE__, __LINE__, (message))

#endif
