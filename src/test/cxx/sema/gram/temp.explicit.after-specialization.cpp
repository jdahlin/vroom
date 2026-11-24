// [temp.explicit]: an explicit instantiation of an explicitly specialized class has no effect
template <class T> struct A {};
template <> struct A<float>;
template struct A<float>;
template <class T> struct B { int f() { return 1; } };
template <> struct B<int> { int f(); };
template struct B<int>;
