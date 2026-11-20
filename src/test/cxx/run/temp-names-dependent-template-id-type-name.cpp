/* [temp.names]: a dependent template-id as a type in a function template body */
template <class T> struct B { T v; };
template <class T> int f() { B<T> b{T(3)}; B<B<T> > c{b}; return (int)c.v.v; }
int main() { return f<int>() - 3; }
