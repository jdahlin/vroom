/*
 * mmap for the WebAssembly build.  vcc reserves gigabytes of address space
 * (MAP_NORESERVE) and touches only what a job uses; linear memory has no
 * holes, so here a reservation is a stretch of fresh memory from sbrk (zero,
 * and the browser commits its pages only when touched), and a NORESERVE one
 * is cut to WASM_RESERVE_MAX, any other above WASM_BIG to WASM_BIG_CUT
 * (the shared header region, the interner's copy buffer; the hash tables are
 * smaller): enough for the small files of the explorer, not for the kernel.  Nothing is unmapped: the page runs every compile in a
 * new instance.  Files are read into memory.
 */
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define WASM_RESERVE_MAX ((size_t)16 << 20)
#define WASM_BIG ((size_t)64 << 20)
#define WASM_BIG_CUT ((size_t)32 << 20)

static void *grab(size_t n)
{
	uintptr_t cur = (uintptr_t)sbrk(0), al = (cur + 65535) & ~(uintptr_t)65535;

	n = (n + 65535) & ~(size_t)65535;
	if (sbrk((intptr_t)(al - cur + n)) == (void *)-1)
		{ fprintf(stderr, "wasm mmap: %zu bytes failed (brk %zu)\n", n, (size_t)cur); return MAP_FAILED; }
	return (void *)al;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	void *p;

	(void)prot;
	if (flags & MAP_FIXED) {
		if (flags & MAP_ANON) {
			memset(addr, 0, len);
			return addr;
		}
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (flags & MAP_ANON) {
		if (len > WASM_BIG)
			len = WASM_BIG_CUT;
		else if ((flags & MAP_NORESERVE) && len > WASM_RESERVE_MAX)
			len = WASM_RESERVE_MAX;
		return grab(len);
	}
	p = grab(len);
	if (p != MAP_FAILED)
		for (size_t got = 0; got < len;) {
			ssize_t r = pread(fd, (char *)p + got, len - got, off + (off_t)got);

			if (r <= 0)
				break;
			got += (size_t)r;
		}
	return p;
}

int munmap(void *addr, size_t len)
{
	(void)addr, (void)len;
	return 0;
}

int madvise(void *addr, size_t len, int advice)
{
	(void)addr, (void)len, (void)advice;
	return 0;
}
