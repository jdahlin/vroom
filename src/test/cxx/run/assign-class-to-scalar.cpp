// x = v and x += v of a scalar from a class object: its conversion function (json's position_t to size_t)
typedef unsigned long size_t;
struct P { size_t total = 7; constexpr operator size_t() const { return total; } };
struct L { P position; P get() const { return position; } };
size_t a(L *m) { size_t x; x = m->get(); return x; }
size_t b(L *m) { size_t x = m->get(); return x; }
size_t c(P p) { size_t x; x = p; return x; }
size_t d(L *m) { return m->get() - 1; }
int main() { L l; return a(&l) + b(&l) + c(l.position) + d(&l) - 27; }
