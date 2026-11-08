/*
 * rsh: the subset of sh that kbuild's object and archive recipes use, run
 * inside vccd so such a recipe costs no shell and no tool processes.
 *
 * A script is parsed whole before anything runs; rsh_run returns
 * RSH_DECLINE (and has done nothing) when it uses anything outside the
 * subset, and the caller runs it with a real shell instead.  The subset:
 *
 *   list      cmd { ';' cmd } [';']
 *   cmd       set -e | : | true | trap WORD... | echo WORD... [> FILE]
 *             | printf FMT WORD... [> FILE] | rm -f WORD...
 *             | [NAME=WORD...] TOOL WORD...
 *             | printf FMT WORD... '|' xargs TOOL WORD...
 *             | .../scripts/basic/fixdep DEP TARGET CMD > FILE
 *   WORD      unquoted parts and \X, '...', "..." without $ ` \; no globs
 *
 * TOOL is a program the caller runs itself (ops->is_tool); the assignments
 * before it are its environment.  printf knows %s, %% and \n \t \\; echo
 * takes no options and no backslashes.  Traps are accepted and ignored
 * (they only delete the target when the shell is killed, which make also
 * does).  Paths are relative to cwd.
 */
#ifndef VCC_RSH_H
#define VCC_RSH_H
#include <stdbool.h>

#define RSH_DECLINE (-1)

struct rsh_ops {
	void *ctx;
	bool (*is_tool)(void *ctx, const char *argv0);
	/* argv and assign are NULL-terminated; fds are the tool's 0, 1, 2 */
	int (*run_tool)(void *ctx, char **argv, char **assign, const int fds[3]);
	/* kbuild's fixdep, writing cmdfile (and removing dep, as the recipe does next) */
	int (*fixdep)(void *ctx, const char *dep, const char *target, const char *cmd,
		      const char *cmdfile, int errfd);
};

/* errexit: started as sh -ec */
int rsh_run(const char *cwd, const char *script, bool errexit, const int fds[3],
	    const struct rsh_ops *ops);

#endif
