/* Darwin thread-locals are reached through a TLV thunk (blr x16) that keeps
 * every register but x0: values held in x9-x15 across it must survive, and
 * the clean-ups must not take the access for a call that clobbers x0-x18.
 * (Elsewhere the test runs on plain globals: the bare-metal runtime has no TLS.) */
#ifdef __APPLE__
#define TL _Thread_local
#else
#define TL
#endif

struct st { long *a, *b; long n; struct st *next; };

struct st *take(int fresh);
long *seen_a(void);
long *seen_b(void);
long seen_n(void);
