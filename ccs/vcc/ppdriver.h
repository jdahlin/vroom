#ifndef VCC_PPDRIVER_H
#define VCC_PPDRIVER_H
typedef struct ppd_env {
	const char *cwd;	/* relative paths are relative to this (NULL: process cwd) */
	int outfd, errfd;	/* output without -o; diagnostics */
} ppd_env;

/* one -E job; argv[0] is ignored.  Returns the exit status. */
int ppd_run(int argc, char **argv, const ppd_env *env);
/* token-by-token comparison of ours[0..on) with the file refpath (0: same) */
int ppd_compare(const char *ours, size_t on, const char *refpath);
#endif
