struct P { int kitty; int x; };
#define PC(s) ((s)->ca ## t)
int p(struct P *s) { return s->kitty + PC(s) + s->x; }
