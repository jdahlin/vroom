struct P { int cat; int x; };
#define PC(s) ((s)->ca ## t)
int p(struct P *s) { return s->cat + PC(s) + s->x; }
