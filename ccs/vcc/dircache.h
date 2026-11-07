#ifndef VCC_DIRCACHE_H
#define VCC_DIRCACHE_H
#include <stddef.h>
/*
 * 0 if path cannot exist because its directory does not (known without a
 * syscall after the first probe of that directory), -1 if open() must
 * decide.  Shared by all threads.
 */
int vdir_exists(const char *path, size_t len);
#endif
