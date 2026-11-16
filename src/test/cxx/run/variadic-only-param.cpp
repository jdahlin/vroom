// f(...): a function whose only parameter is ..., defined and called (a trivial class passed through it)
struct A { int x; };
static int f(...) { return 7; }
template <class T> int g(...) { return 3; }
int main() { A a{1}; return f(a) + f() + g<int>(1, 2) == 17 ? 0 : 1; }
