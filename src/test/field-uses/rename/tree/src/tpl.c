struct T1 { int count; };
struct T2 { int count; };
#define COUNT(p) ((p)->count)
int g(struct T1 *a, struct T2 *b) { return COUNT(a) + COUNT(b); }
