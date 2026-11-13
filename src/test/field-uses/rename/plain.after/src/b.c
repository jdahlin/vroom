#include "box.h"
#define LEN(b) ((b)->length)
int f(struct Box *b) { return LEN(b) + b->position; }
