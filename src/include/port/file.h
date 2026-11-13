/*
 * A whole file mapped read-only (vfile_map; an empty file maps to p = NULL,
 * n = 0), from ar.
 */
#ifndef VCC_PORT_FILE_H
#define VCC_PORT_FILE_H

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common/attributes.h"
#include "common/scoped-cleanup.h"

typedef struct vfile_mapping {
	const unsigned char *p;
	size_t n;
} vfile_mapping;

static inline VUSE VNONNULL_ALL bool vfile_map(const char *abs, vfile_mapping *m, struct stat *st)
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

#endif
