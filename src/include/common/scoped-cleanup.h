/*
 * Scoped resources through __attribute__((cleanup)): a variable that gives
 * its resource back on every exit from its block (return, break, goto), so
 * the error paths of the tools do not have to repeat the frees.
 *
 *   VAUTOFREE char *p = malloc(n);      free(p) at the end of the block
 *   VAUTOCLOSE int fd = open(...);      close(fd) unless fd < 0
 *   (VAUTOUNMAP, for a mapped file, is in port/file.h)
 *   VLOCK(mu);                          pthread_mutex_unlock(&mu) at the end
 *   VLOCK_AS(g, mu);                    ... with a name, to drop it early:
 *   VUNLOCK(g);                         unlocks now (and not again)
 *
 * Rules:
 *   - A cleanup runs when the block is left normally; longjmp and exit()
 *     skip it.  Leave a lock before a longjmp (emit's efatal does) and use
 *     these only where nothing jumps out of the scope.
 *   - To hand the resource over, take it out first: VTAKE(p) returns the
 *     pointer and leaves NULL behind, VTAKE_FD(fd) returns the fd and
 *     leaves -1.
 *   - Initialise before any goto that can reach the end of the block: the
 *     cleanup of an uninitialised variable frees garbage.  (A goto past the
 *     declaration into its scope is an error in clang and in vcc.)
 *
 * The cleanup functions are static inline: nothing here costs a call at -O2.
 */
#ifndef VCC_COMMON_SCOPED_CLEANUP_H
#define VCC_COMMON_SCOPED_CLEANUP_H

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#define VCLEANUP(f) __attribute__((cleanup(f)))

static inline void vdefer_free(void *pp)
{
	void **p = pp;

	free(*p);
	*p = NULL;
}
#define VAUTOFREE VCLEANUP(vdefer_free)

static inline void vdefer_close(int *fd)
{
	if (*fd >= 0)
		close(*fd);
	*fd = -1;
}
#define VAUTOCLOSE VCLEANUP(vdefer_close)
#define VCLOSE(fd) vdefer_close(&(fd))	/* close now, early: the cleanup then finds -1 */

static inline void vdefer_unlock(pthread_mutex_t **m)
{
	if (*m) {
		pthread_mutex_unlock(*m);
		*m = NULL;
	}
}

static inline pthread_mutex_t *vdefer_lock(pthread_mutex_t *m)
{
	pthread_mutex_lock(m);
	return m;
}

#define VDEFER_CAT2(a, b) a##b
#define VDEFER_CAT(a, b) VDEFER_CAT2(a, b)
#define VLOCK_AS(g, mu) pthread_mutex_t *g VCLEANUP(vdefer_unlock) = vdefer_lock(&(mu))
#define VLOCK(mu) VLOCK_AS(VDEFER_CAT(vlock_, __LINE__), mu)
#define VUNLOCK(g) vdefer_unlock(&(g))

/* take the resource out of its variable (so the cleanup finds nothing) */
#define VTAKE(p) ({ __typeof__(p) vtake_ = (p); (p) = NULL; vtake_; })
#define VTAKE_FD(fd) ({ int vtake_ = (fd); (fd) = -1; vtake_; })

#endif
