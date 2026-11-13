/*
 * output.c: everything a job writes: files, the per-thread object state, and the object itself.
 *
 * Files are written to a temporary next to the target and renamed over it, so an interrupted or failed
 * job leaves the old file or none, never a torn one (a device or fifo is written in place).  A new
 * file gets the client's umask (driver_client_umask, set by the vccd dispatcher) and a job whose client has
 * gone (driver_client_connection closed) writes nothing.  The object state (assembler, ElfObj, 4 GB reserved output
 * buffer) is expensive to build, so a thread takes one from a locked free list at the start of a -c
 * job (obj_state) and returns it at the end (driver_object_state_release); driver_assembler, driver_object and driver_object_buffer are valid in
 * between only.  driver_object_write lays out the object (ELF, or Mach-O for Darwin targets) and writes it
 * with one write.  Failures are reported on the job's error fd and returned as 1.
 */
#include "driver-internal.h"

/* path relative to cwd (if set) */
VPRIV const char *at(const char *cwd, const char *path, char *buf, size_t cap)
{
	if (!cwd || path[0] == '/')
		return path;
	snprintf(buf, cap, "%s/%s", cwd, path);
	return buf;
}

VPRIV int write_all(int fd, const char *p, size_t n)
{
	while (n) {
		ssize_t w = write(fd, p, n);

		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return -1;
		p += w;
		n -= (size_t)w;
	}
	return 0;
}

/* outputs go to a temporary next to the target, renamed over it when complete:
   an interrupted or failed job leaves the old file or none, never a torn one */
_Thread_local int driver_client_umask = -1;
_Thread_local int driver_client_connection = -1;

/* the client closed its end (it was killed, or make was interrupted): the
 * request is all it sends, so anything readable now is the EOF */
VPRIV bool driver_client_is_gone(void)
{
	struct pollfd pf = { driver_client_connection, POLLIN, 0 };
	char c;

	return driver_client_connection >= 0 && poll(&pf, 1, 0) == 1 && recv(driver_client_connection, &c, 1, MSG_PEEK | MSG_DONTWAIT) == 0;
}

/* a new output's mode: the client's umask under vccd */
VPRIV void driver_output_mode_set(int fd)
{
	if (driver_client_umask >= 0)
		fchmod(fd, 0666 & ~(mode_t)driver_client_umask);
}

VPRIV bool driver_output_temporary_name(char *t, size_t cap, const char *f)
{
	static _Atomic unsigned seq;
	struct stat st;

	/* /dev/null, a fifo, ...: written where it is, a rename would replace it */
	if (!stat(f, &st) && !S_ISREG(st.st_mode)) {
		snprintf(t, cap, "%s", f);
		return false;
	}
	snprintf(t, cap, "%s.vcc%d.%u.tmp", f, (int)getpid(), atomic_fetch_add(&seq, 1));
	return true;
}

VPRIV int write_file(const char *cwd, const char *path, const char *s, size_t n, int errfd)
{
	char b[8192], t[8300];
	const char *f = at(cwd, path, b, sizeof(b));
	int fd;

	bool tmp = driver_output_temporary_name(t, sizeof(t), f);

	if (driver_client_is_gone()) {
		dprintf(errfd, "vcc: %s not written: the client has gone\n", path);
		return 1;
	}

	fd = open(t, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
	if (fd >= 0 && tmp)
		driver_output_mode_set(fd);
	if (fd < 0 || write_all(fd, s, n) || close(fd) || (tmp && rename(t, f))) {
		int e = errno;

		dprintf(errfd, "vcc: error: cannot write %s: %s\n", path, strerror(e));
		if (fd >= 0 && tmp)
			unlink(t);
		return 1;
	}
	return 0;
}

VPRIV int write_out(const char *cwd, const char *out, int outfd, const char *s, size_t n, int errfd)
{
	if (out && strcmp(out, "-"))
		return write_file(cwd, out, s, n, errfd);
	return write_all(outfd, s, n) ? 1 : 0;
}

/* phase times over all -c jobs (ns): front end + codegen, assembler, finish + write */
_Atomic unsigned long long driver_compile_nanoseconds, driver_emit_nanoseconds, driver_object_nanoseconds, driver_object_bytes, driver_tokens_taken, driver_tokens_skipped;
_Thread_local vperf driver_phase_counters[DRIVER_PHASE_COUNT];

VPRIV unsigned long long driver_clock_nanoseconds(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

/*
 * a job's object state: assembler, ELF writer, serialised output; from a
 * process-wide pool, so it outlives threads (vpp --batch starts some per pass)
 */

_Thread_local as_context *driver_assembler;
_Thread_local ElfObj *driver_object;
_Thread_local uint8_t *driver_object_buffer;
#define DRIVER_OBJECT_BUFFER_CAPACITY ((size_t)1 << 32)



static struct driver_object_state { as_context *as; ElfObj *obj; uint8_t *buf; struct driver_object_state *next; } *driver_object_state_free_list;
static _Thread_local struct driver_object_state *driver_object_state_current;
static pthread_mutex_t driver_object_state_lock = PTHREAD_MUTEX_INITIALIZER;


VPRIV void obj_state(void)
{
	struct driver_object_state *s;

	if (driver_object_state_current)
		return;
	pthread_mutex_lock(&driver_object_state_lock);
	s = driver_object_state_free_list;
	if (s)
		driver_object_state_free_list = s->next;
	pthread_mutex_unlock(&driver_object_state_lock);
	if (!s) {
		s = calloc(1, sizeof(*s));
		s->as = as_new();
		s->obj = elf_new();
		s->buf = mmap(NULL, DRIVER_OBJECT_BUFFER_CAPACITY, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
		if (s->buf == MAP_FAILED)
			abort();
	}
	driver_object_state_current = s;
	driver_assembler = s->as, driver_object = s->obj, driver_object_buffer = s->buf;
}


VPRIV void driver_object_state_release(void)
{
	struct driver_object_state *s = driver_object_state_current;

	if (!s)
		return;
	driver_object_state_current = NULL;
	driver_assembler = NULL, driver_object = NULL, driver_object_buffer = NULL;
	pthread_mutex_lock(&driver_object_state_lock);
	s->next = driver_object_state_free_list;
	driver_object_state_free_list = s;
	pthread_mutex_unlock(&driver_object_state_lock);
}

/* -c for wasm64: the object's bytes (arch/wasm/as, platform/wasm) */

_Thread_local vbuffer driver_wasm_object;



/* lay out driver_object and write it to out */

VPRIV int driver_object_write(const char *cwd, const char *out, int outfd, int errfd, int macho)
{
	unsigned long long t0 = driver_clock_nanoseconds();
	vperf v0 = vperformance_now();
	size_t n = macho ? macho_finish(driver_object) : elf_finish(driver_object);
	int r;

	if (!n || driver_object->has_err) {
		dprintf(errfd, "vcc: error: %s\n", driver_object->err[0] ? driver_object->err : "cannot lay out the object");
		return 1;
	}
	if (n > DRIVER_OBJECT_BUFFER_CAPACITY) {
		dprintf(errfd, "vcc: error: object too large\n");
		return 1;
	}
	if (macho)
		macho_emit(driver_object, driver_object_buffer);
	else
		elf_emit(driver_object, driver_object_buffer);
	r = write_out(cwd, out, outfd, (const char *)driver_object_buffer, n, errfd);
	driver_object_nanoseconds += driver_clock_nanoseconds() - t0;
	driver_phase_add(DRIVER_PHASE_OBJECT, v0);
	driver_object_bytes += n;
	return r;
}

/* the default object name: the source's basename with .o */
VPRIV const char *driver_object_name(const char *main_file, char *buf, size_t cap)
{
	const char *b = strrchr(main_file, '/') ? strrchr(main_file, '/') + 1 : main_file;
	const char *dot = strrchr(b, '.');

	snprintf(buf, cap, "%.*s.o", (int)(dot ? dot - b : (long)strlen(b)), b);
	return buf;
}
