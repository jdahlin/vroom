#include "box.h"
#define LEN(b) ((b)->cur)
int f(struct Box *b) { return LEN(b) + b->len; }
