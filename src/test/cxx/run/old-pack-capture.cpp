// a parameter pack's elements captured by reference in a lambda (each element its own capture)
static int add(int x, long y) { return x + (int)y; }
template <class... A> int f(A... a) { int r = 0; [&] { r = add(a...); }(); return r; }
int main() { return f(2, 3L) == 5 ? 0 : 1; }
