/* [dcl.init]: new (p) A({...}) of an aggregate constructs the object from
   the list (no move) */
void *operator new(unsigned long, void *p) noexcept { return p; }
extern "C" int printf(const char *, ...);
struct M { int v; M(int x) : v(x) { printf("make %d\n", v); } M(M &&o) : v(o.v) { o.v = 0; printf("move %d\n", v); } ~M() { printf("drop %d\n", v); } };
struct VP { M m; int q; };
int main() { alignas(VP) char b[sizeof(VP)]; VP *p = new (b) VP({M{3}, 5}); printf("%d %d\n", p->m.v, p->q); p->~VP(); }
