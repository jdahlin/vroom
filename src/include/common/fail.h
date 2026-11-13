/*
 * The job's escape: a server-wide table or region is full.  vjob_escape
 * reports msg on vjob_escape_fd, sets vtables_full and calls the job's hook
 * (the driver's: an error for this TU, longjmp); without a hook it aborts.
 */
#ifndef VCC_FAIL_H
#define VCC_FAIL_H

/* called (in the thread) when the table is full; must not return to the caller */
extern void (*vintern_full_hook)(void);	/* (the job's escape, see vjob_escape) */
__attribute__((noreturn)) void vjob_escape(const char *msg);
extern _Thread_local int vjob_escape_fd;	/* where vjob_escape reports: the job's diagnostics (2) */
extern _Atomic int vtables_full;	/* set once the table was found full (vccd restarts) */

#endif
