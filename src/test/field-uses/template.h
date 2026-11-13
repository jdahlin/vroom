/* the same text with two structs */
#define ACCESS(p) ((p)->count)
static inline int TPL(get)(struct BOX *b) { return ACCESS(b); }
