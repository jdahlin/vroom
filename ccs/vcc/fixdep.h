#ifndef VCC_FIXDEP_H
#define VCC_FIXDEP_H
/*
 * scripts/basic/fixdep DEPFILE TARGET CMDLINE > CMDFILE; rm -f DEPFILE,
 * in process (relative paths against cwd).  Returns fixdep's exit status;
 * messages go to errfd.
 */
int vfixdep(const char *cwd, const char *depfile, const char *target, const char *cmdline, const char *cmdfile,
	    int errfd);
#endif
