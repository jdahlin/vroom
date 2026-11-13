#include "box.h"
struct Other { int len; };
int len_of(struct Box *b, struct Other *o)
{
	int len = o->len;	/* a local and another struct's field */
	struct Box v = { .cur = 1, .len = 2 };

	return b->cur + v.len + len + box_len(b) + (int)__builtin_offsetof(struct Box, len);
}
#if 0
int dead(struct Box *b) { return b->len; }
#endif
/* the Box `len` is compared with `cur` */
