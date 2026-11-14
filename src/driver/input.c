/*
 * input.c: read the main file whole into memory when it is `-`, a device or a pipe.
 *
 * Regular files are mapped by the preprocessor itself; this covers what cannot be (stdin, kbuild's
 * probes that compile /dev/null).  The lexer wants one padded piece, so the input is read to the end
 * into a per-thread buffer that is reused by the next job on the thread (the result points into it:
 * valid until the thread's next driver_input_stdin_read).  Under vccd the client's stdin comes from env->infd,
 * or already read (env->input_text, which vccd keeps for a retry).  Fails the job (status 1, message on
 * the job's error fd) on a read error or input of 2 GB or more; assembler input from stdin is a usage
 * error (status 2).
 */
#include "driver-internal.h"

/* the input `-`, whole (the lexer wants it padded in one piece); nonzero after an error (reported) */
static __attribute__((noinline, cold)) int driver_input_stdin_read(int fd, int errfd, const char **src, size_t *len)
{
	static _Thread_local char *ib;
	static _Thread_local size_t icap;
	size_t n = 0;

	for (;;) {
		ssize_t k;

		if (icap - n < 65536) {
			char *nb = realloc(ib, icap = icap ? icap * 2 : 1 << 20);

			if (!nb) {
				dprintf(errfd, "vcc: error: out of memory reading stdin\n");
				return 1;
			}
			ib = nb;
		}
		k = read(fd, ib + n, icap - n);
		if (k < 0 && errno == EINTR)
			continue;
		if (k < 0) {
			dprintf(errfd, "vcc: error: cannot read stdin: %s\n", strerror(errno));
			return 1;
		}
		if (k == 0)
			break;
		n += (size_t)k;
		if (n >= (1u << 31)) {
			dprintf(errfd, "vcc: error: <stdin>: file too large (2 GB or more)\n");
			return 1;
		}
	}
	*src = ib, *len = n;
	return 0;
}

/* 0 to go on; 1: the job ends (status and message set) */
VPRIV int driver_input_read(struct driver_job *job, const driver_environment *env)
{
	if (!strcmp(job->main_file, "-") && job->raw_assembler == 1) {
		dprintf(job->errfd, "vcc: error: assembler input from stdin is not supported\n");
		job->status = 2;
		return 1;
	}
	if (!strcmp(job->main_file, "-") && env->input_text) {
		job->stdin_src = env->input_text, job->stdin_len = env->input_length;
	} else if (!strcmp(job->main_file, "-")) {
		if (driver_input_stdin_read(env->infd, job->errfd, &job->stdin_src, &job->stdin_len)) {
			job->status = 1;
			return 1;
		}
	} else {
		/* a device or pipe (kbuild's probes compile /dev/null): read whole, as stdin */
		struct stat dst;

		if (!stat(job->main_file, &dst) && !S_ISREG(dst.st_mode) && !S_ISDIR(dst.st_mode)) {
			int dfd = open(job->main_file, O_RDONLY);

			if (dfd < 0 || driver_input_stdin_read(dfd, job->errfd, &job->stdin_src, &job->stdin_len)) {
				if (dfd >= 0)
					close(dfd);
				dprintf(job->errfd, "vcc: error: %s: cannot read\n", job->main_file);
				job->status = 1;
				return 1;
			}
			close(dfd);
		}
	}
	return 0;
}
