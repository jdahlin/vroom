/* [temp.names]: template-id in a base-specifier and mem-initializer */
template <class T> struct B { int v; B(int x) : v(x) {} };
struct D : B<int> { D() : B<int>(5) {} };
int main() { D d; return d.v - 5; }
