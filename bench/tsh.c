/*
 * tsh: a tracing SHELL for make.  Runs the real shell ($TSH_REAL, default
 * /bin/sh) with the same arguments and appends one line per invocation to
 * $TSH_LOG:
 *
 *   start_ms end_ms pid ppid status user_ms sys_ms cwd-tail | command
 *
 * Times are CLOCK_MONOTONIC in ms (the clock vccd's VCC_LOG uses), user/sys
 * are the shell's and its children's rusage.  Used by bench/kprof.py.
 *   cc -O2 -o build/ccs/tsh bench/tsh.c
 */
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
	const char *real = getenv("TSH_REAL"), *logp = getenv("TSH_LOG");
	double t0 = now_ms(), t1;
	struct rusage ru;
	char buf[4096], cwd[1024];
	int st = 0, n, fd, i;
	pid_t pid;

	(void)argc;
	if (!real)
		real = "/bin/sh";
	argv[0] = (char *)real;
	if (posix_spawn(&pid, real, NULL, NULL, argv, environ)) {
		perror("tsh: posix_spawn");
		return 127;
	}
	while (wait4(pid, &st, 0, &ru) < 0 && errno == EINTR)
		;
	t1 = now_ms();
	if (logp && (fd = open(logp, O_WRONLY | O_APPEND | O_CREAT, 0644)) >= 0) {
		const char *c = getcwd(cwd, sizeof(cwd)) ? cwd : "?";
		const char *s = strlen(c) > 40 ? c + strlen(c) - 40 : c;

		n = snprintf(buf, sizeof(buf), "%.3f %.3f %d %d %d %.1f %.1f %s |", t0, t1,
			     (int)pid, (int)getppid(), WIFEXITED(st) ? WEXITSTATUS(st) : 128,
			     ru.ru_utime.tv_sec * 1e3 + ru.ru_utime.tv_usec / 1e3,
			     ru.ru_stime.tv_sec * 1e3 + ru.ru_stime.tv_usec / 1e3, s);
		for (i = 1; argv[i] && n < (int)sizeof(buf) - 2; i++) {
			const char *a = argv[i];

			buf[n++] = ' ';
			for (; *a && n < (int)sizeof(buf) - 2; a++)
				buf[n++] = (*a == '\n' || *a == '\t') ? ' ' : *a;
		}
		buf[n++] = '\n';
		write(fd, buf, n);
		close(fd);
	}
	if (WIFSIGNALED(st))
		return 128 + WTERMSIG(st);
	return WEXITSTATUS(st);
}
