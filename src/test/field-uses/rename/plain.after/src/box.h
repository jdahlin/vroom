struct Box { int length; int position; };
static inline int box_len(struct Box *b) { return b->length; }
