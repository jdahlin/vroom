struct Box { int cur; int len; };
static inline int box_len(struct Box *b) { return b->cur; }
