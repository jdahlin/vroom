#ifndef VCC_AR_H
#define VCC_AR_H

/*
 * ar r/m/t on thin archives, byte-identical to llvm-ar for Kbuild's uses,
 * and regular GNU and COFF archives (ar.c).  cwd: the request's directory (paths are resolved against it,
 * nothing changes the process's); t lists to outfd; messages go to errfd.
 * Returns the exit status.  Thread-safe.
 */
int ar_run(const char *cwd, int argc, char **argv, int outfd, int errfd);

#endif
