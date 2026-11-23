/* [temp.names]: an alias template-id, and as an argument of another template-id */
template <class T> struct Box { T v; };
template <class T> using BoxOf = Box<T>;
template <class T> using Twice = BoxOf<BoxOf<T> >;
int main() { Twice<int> t{{5}}; return t.v.v - 5; }
