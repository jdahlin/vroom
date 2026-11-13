#include "box.h"
struct Other { int len; };
int len_of(struct Box *b, struct Other *o)
{
	int len = o->len;	/* a local and another struct's field */
	struct Box v = { .length = 1, .position = 2 };

	return b->length + v.position + len + box_len(b) + (int)__builtin_offsetof(struct Box, position);
}
#if 0
int dead(struct Box *b) { return b->len; }
#endif
/* the Box `len` is compared with `cur` */
