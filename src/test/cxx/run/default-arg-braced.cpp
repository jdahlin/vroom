// default arguments = {}: a scalar's is zero (int, enum, pointer, bool), a class's a value-initialized temporary
struct P { int x = 3; };
enum E { e0, e1 };
int g(P p = {}) { return p.x; }
int h(int i = {}, E e = {}, const char *s = {}, bool b = {}) { return i + e + (s != nullptr) + b; }
struct J { static int k(long n = {}) { return (int)n; } };
int main() { return g() - 3 + h() + h(1, e1) - 2 + J::k(); }
