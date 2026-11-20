/* [temp.names]: a template name as a template template argument */
template <class T> struct Box { T v; };
template <template <class> class C, class T> struct Wrap { C<T> c; };
int main() { Wrap<Box, int> w{{6}}; return w.c.v - 6; }
