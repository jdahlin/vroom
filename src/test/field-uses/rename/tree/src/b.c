#include "box.h"
#define LEN(b) ((b)->len)
int f(struct Box *b) { return LEN(b) + b->cur; }
