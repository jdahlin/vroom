struct Anon { int x; union { int y; long w; }; };
int anon(struct Anon *a) { return a->x + a->y; }
