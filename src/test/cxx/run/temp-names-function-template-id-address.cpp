/* [temp.names]: a function template-id used as a value (address, overload set argument) */
template <class T> int id(T x) { return (int)x; }
int apply(int (*f)(int), int v) { return f(v); }
int main() { auto p = &id<int>; return p(2) - 2 + apply(id<int>, 3) - 3; }
