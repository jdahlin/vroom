/* Running a program with the request's cwd, environment and file descriptors. */
#include "vccd-internal.h"

/* run a program with the request's cwd, env and fds */
VPRIV int spawn(const struct req *r, const char *prog)
{
	posix_spawn_file_actions_t fa;
	char *saved = r->argv[0];
	pid_t pid;
	int st, i, err;

	/* the request's environment minus the dynamic loader's: a client's
	   DYLD_INSERT_LIBRARIES / LD_PRELOAD must not run code in our children */
	size_t ne = 0, nall = 0;
	char **env;

	while (r->envp[nall])
		nall++;
	env = malloc((nall + 1) * sizeof(char *));
	for (char **e = r->envp; *e; e++)
		if (strncmp(*e, "DYLD_", 5) && strncmp(*e, "LD_PRELOAD=", 11) && strncmp(*e, "LD_LIBRARY_PATH=", 16) &&
		    strncmp(*e, "LD_AUDIT=", 9) && strncmp(*e, "VCC_UMASK=", 10))
			env[ne++] = *e;
	env[ne] = NULL;
	posix_spawn_file_actions_init(&fa);
	for (i = 0; i < 3; i++)
		if (r->fds[i] >= 0)
			posix_spawn_file_actions_adddup2(&fa, r->fds[i], i);
	posix_spawn_file_actions_addchdir(&fa, r->cwd);
	r->argv[0] = (char *)prog;
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
	/* only 0-2 reach the child: other workers' request fds are open meanwhile */
	posix_spawnattr_t sa;

	posix_spawnattr_init(&sa);
	posix_spawnattr_setflags(&sa, POSIX_SPAWN_CLOEXEC_DEFAULT);
	err = posix_spawn(&pid, prog, &fa, &sa, r->argv, env);
	posix_spawnattr_destroy(&sa);
#else
	err = posix_spawn(&pid, prog, &fa, NULL, r->argv, env);
#endif
	r->argv[0] = saved;
	free(env);
	posix_spawn_file_actions_destroy(&fa);
	if (err) {
		say(r, "%s: error: cannot run %s\n", prog);
		return 127;
	}
	while (waitpid(pid, &st, 0) < 0)
		if (errno != EINTR)
			return 127;
	return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}
