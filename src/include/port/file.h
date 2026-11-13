/*
 * Files, over POSIX: the one place the tools map, read and write whole files.
 *
 *   vfile_map(abs, &m, &st)          a whole file mapped read-only (an empty file maps
 *                                    to p = NULL, n = 0); false with errno set on any
 *                                    failure (open, fstat, mmap).  Also filled: st
 *   vfile_map_at(dirfd, path, &m)    the same relative to a directory (AT_FDCWD for the
 *                                    process's); no stat result
 *   vfile_unmap(&m)                  and VAUTOUNMAP, which does it at the end of a block
 *   vfile_read(dirfd, path, &n)      the whole file in malloc'ed memory with a NUL after
 *                                    its n bytes (free() it), or NULL with errno set
 *   vfile_write_all(fd, p, n)        write all n bytes, continuing after a short write and
 *                                    retrying on EINTR; false with errno set on an error
 *   vpath_join(dir, path, out, cap)  `path` as is when it is absolute, else dir/path (no
 *                                    doubled slash); false when it does not fit in cap bytes
 *
 * None of them fails the job: the caller decides what an error means.
 */
#ifndef VCC_PORT_FILE_H
#define VCC_PORT_FILE_H

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common/attributes.h"
#include "common/scoped-cleanup.h"

typedef struct vfile_mapping {
	const unsigned char *p;
	size_t n;
} vfile_mapping;

/* map an open file (st is its fstat) */
static inline bool vfile_map_fd_(int fd, vfile_mapping *m, struct stat *st)
{
	void *p;

	m->p = NULL;
	m->n = 0;
	if (fstat(fd, st))
		return false;
	if (st->st_size) {
		p = mmap(NULL, st->st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (p == MAP_FAILED)
			return false;
		m->p = p;
		m->n = st->st_size;
	}
	return true;
}

static inline VUSE VNONNULL_ALL bool vfile_map(const char *abs, vfile_mapping *m, struct stat *st)
{
	int fd = open(abs, O_RDONLY);
	bool ok;
	int saved;

	m->p = NULL;
	m->n = 0;
	if (fd < 0)
		return false;
	ok = vfile_map_fd_(fd, m, st);
	saved = errno;
	close(fd);
	errno = saved;
	return ok;
}

static inline VUSE VNONNULL_ALL bool vfile_map_at(int dirfd, const char *path, vfile_mapping *m)
{
	struct stat st;
	int fd = openat(dirfd, path, O_RDONLY);
	bool ok;
	int saved;

	m->p = NULL;
	m->n = 0;
	if (fd < 0)
		return false;
	ok = vfile_map_fd_(fd, m, &st);
	saved = errno;
	close(fd);
	errno = saved;
	return ok;
}

static inline VNONNULL_ALL void vfile_unmap(vfile_mapping *m)
{
	if (m->p)
		munmap((void *)m->p, m->n);
}

static inline void vdefer_unmap(vfile_mapping *m)
{
	vfile_unmap(m);
	m->p = NULL;
	m->n = 0;
}
#define VAUTOUNMAP VCLEANUP(vdefer_unmap)	/* VAUTOUNMAP vfile_mapping m; unmaps at the end of the block */

static inline VUSE VNONNULL_ALL void *vfile_read(int dirfd, const char *path, size_t *n)
{
	int fd = openat(dirfd, path, O_RDONLY);
	struct stat st;
	char *data;
	size_t got = 0;

	*n = 0;
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st)) {
		int saved = errno;

		close(fd);
		errno = saved;
		return NULL;
	}
	data = malloc((size_t)st.st_size + 1);
	if (!data) {
		close(fd);
		errno = ENOMEM;
		return NULL;
	}
	while (got < (size_t)st.st_size) {
		ssize_t r = read(fd, data + got, (size_t)st.st_size - got);

		if (r < 0 && errno == EINTR)
			continue;
		if (r < 0) {
			int saved = errno;

			free(data);
			close(fd);
			errno = saved;
			return NULL;
		}
		if (r == 0)
			break;	/* (a file that shrank: the bytes there were) */
		got += (size_t)r;
	}
	close(fd);
	data[got] = 0;
	*n = got;
	return data;
}

static inline VNONNULL(2) bool vfile_write_all(int fd, const void *p, size_t n)
{
	const char *c = p;

	while (n) {
		ssize_t w = write(fd, c, n);

		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0) {
			if (w == 0)
				errno = EIO;
			return false;
		}
		c += w;
		n -= (size_t)w;
	}
	return true;
}

static inline VNONNULL_ALL bool vpath_join(const char *dir, const char *path, char *out, size_t cap)
{
	size_t dn = strlen(dir), pn = strlen(path);

	if (path[0] == '/' || !dn) {
		if (pn + 1 > cap)
			return false;
		memcpy(out, path, pn + 1);
		return true;
	}
	if (dir[dn - 1] == '/')
		dn--;
	if (dn + 1 + pn + 1 > cap)
		return false;
	memcpy(out, dir, dn);
	out[dn] = '/';
	memcpy(out + dn + 1, path, pn + 1);
	return true;
}

#endif
