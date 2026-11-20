#include <memory>
struct P { int x; P(int v) : x(v) {} };
int main() { auto u = std::make_unique<P>(1); std::shared_ptr<P> s = std::make_shared<P>(2); return u->x + s->x == 3 ? 0 : 1; }
