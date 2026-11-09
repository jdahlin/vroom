/*
 * A whole file mapped read-only (vmap_file; an empty file maps to p = NULL,
 * n = 0), from ar.
 */
#ifndef VCC_MAPFILE_H
#define VCC_MAPFILE_H

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct vmapped {
	const unsigned char *p;
	size_t n;
} vmapped;

static inline bool vmap_file(const char *abs, vmapped *m, struct stat *st)
{
	int fd = open(abs, O_RDONLY);
	void *p;

	m->p = NULL;
	m->n = 0;
	if (fd < 0 || fstat(fd, st)) {
		if (fd >= 0)
			close(fd);
		return false;
	}
	if (st->st_size) {
		p = mmap(NULL, st->st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (p == MAP_FAILED) {
			close(fd);
			return false;
		}
		m->p = p;
		m->n = st->st_size;
	}
	close(fd);
	return true;
}

static inline void vunmap_file(vmapped *m)
{
	if (m->p)
		munmap((void *)m->p, m->n);
}

#endif
