struct M { int n; };
#define GETN(s, cnt) ((s)->n)
int h(struct M *m) { return GETN(m, 0); }
