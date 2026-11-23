/* [temp.names]: typename with a dependent template-id */
template <class T> struct A { template <class U> struct B { using type = U; }; };
template <class T> int f() { typename A<T>::template B<int>::type v = 3; return v; }
int main() { return f<char>() - 3; }
