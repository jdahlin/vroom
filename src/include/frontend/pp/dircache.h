#ifndef VCC_DIRCACHE_H
#define VCC_DIRCACHE_H
#include <stddef.h>
/*
 * 0 if path cannot exist because its directory does not (known without a
 * syscall after the first probe of that directory), -1 if open() must
 * decide.  Shared by all threads.
 */
int pp_directory_exists(const char *path, size_t len);
void pp_directory_cache_flush(void);	/* forget every directory (a new build); none in flight */
#endif
