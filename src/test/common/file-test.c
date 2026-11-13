/* Unit test of port/file.h: mapping, reading, writing and joining paths in a scratch directory; errors keep errno and nothing fails the job. */
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include "check.h"
#include "port/file.h"

static volatile sig_atomic_t interrupted;
static void on_alarm(int sig) { (void)sig; interrupted++; }

int main(void)
{
	char dir[] = "/tmp/vcc-file-test-XXXXXX", path[256], big[1 << 16];
	vfile_mapping m;
	struct stat st;
	size_t n;
	char *data;
	int dfd, fd;

	CHECK(mkdtemp(dir) != NULL);
	dfd = open(dir, O_RDONLY);
	CHECK(dfd >= 0);

	CHECK(vpath_join(dir, "a.txt", path, sizeof path) && !strncmp(path, dir, strlen(dir)) && !strcmp(path + strlen(dir), "/a.txt"));
	CHECK(vpath_join("/x/", "y", path, sizeof path) && !strcmp(path, "/x/y"));
	CHECK(vpath_join("/x", "/abs", path, sizeof path) && !strcmp(path, "/abs"));
	CHECK(vpath_join("", "rel", path, sizeof path) && !strcmp(path, "rel"));
	CHECK(!vpath_join("/abcdef", "ghi", path, 11) && vpath_join("/abcdef", "ghi", path, 12));

	/* write, then read, map */
	CHECK(vpath_join(dir, "a.txt", path, sizeof path));
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	CHECK(fd >= 0 && vfile_write_all(fd, "hello", 5) && vfile_write_all(fd, "", 0) && vfile_write_all(fd, " world", 6));
	close(fd);
	data = vfile_read(dfd, "a.txt", &n);
	CHECK(data && n == 11 && !strcmp(data, "hello world"));
	free(data);
	CHECK(vfile_map_at(dfd, "a.txt", &m) && m.n == 11 && !memcmp(m.p, "hello world", 11));
	vfile_unmap(&m);
	CHECK(vfile_map(path, &m, &st) && m.n == 11 && st.st_size == 11);
	{
		VAUTOUNMAP vfile_mapping again = m;
		CHECK(again.p[0] == 'h');
	}

	/* empty file: no mapping, still success */
	fd = open((vpath_join(dir, "empty", path, sizeof path), path), O_WRONLY | O_CREAT, 0600);
	close(fd);
	CHECK(vfile_map_at(dfd, "empty", &m) && m.p == NULL && m.n == 0);
	data = vfile_read(dfd, "empty", &n);
	CHECK(data && n == 0 && data[0] == 0);
	free(data);

	/* errors: errno kept, nothing aborts */
	errno = 0;
	CHECK(!vfile_map_at(dfd, "missing", &m) && errno == ENOENT && m.p == NULL);
	errno = 0;
	CHECK(vfile_read(dfd, "missing", &n) == NULL && errno == ENOENT && n == 0);
	errno = 0;
	CHECK(!vfile_map("/nonexistent/x", &m, &st) && errno == ENOENT);
	errno = 0;
	CHECK(!vfile_write_all(-1, "x", 1) && errno == EBADF);

	/* a write to a pipe larger than its buffer, with signals interrupting it: all bytes arrive */
	{
		int pfd[2];
		struct sigaction sa = { .sa_handler = on_alarm };
		pid_t child;
		size_t total = 0, want = 4 << 20;
		char *src = malloc(want);

		memset(src, 'x', want);
		sigaction(SIGUSR1, &sa, NULL);	/* no SA_RESTART: write returns EINTR */
		CHECK(pipe(pfd) == 0);
		child = fork();
		if (child == 0) {
			close(pfd[1]);
			while (1) {
				ssize_t r = read(pfd[0], big, sizeof big);

				if (r <= 0)
					break;
				total += (size_t)r;
				usleep(100);
			}
			_exit(total == want ? 0 : 1);
		}
		close(pfd[0]);
		{
			pid_t killer = fork();

			if (killer == 0) {
				for (int i = 0; i < 200; i++) {
					kill(getppid(), SIGUSR1);
					usleep(500);
				}
				_exit(0);
			}
			CHECK(vfile_write_all(pfd[1], src, want));
			if (!interrupted)
				fprintf(stderr, "note: no signal interrupted the write\n");
			waitpid(killer, NULL, 0);
		}
		close(pfd[1]);
		{
			int status = 0;

			waitpid(child, &status, 0);
			CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
		}
		free(src);
	}

	close(dfd);
	{
		char cmd[300];

		snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
		CHECK(system(cmd) == 0);
	}
	return check_failures;
}
