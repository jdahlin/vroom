struct Box { int len; int cur; };
static inline int box_len(struct Box *b) { return b->len; }
