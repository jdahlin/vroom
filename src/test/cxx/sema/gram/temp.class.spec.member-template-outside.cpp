// [temp.class.spec]/5: a partial specialization of a member class template,
// defined outside the class template; each specialization of the class has it.
template<class T> struct A { template<class U> struct Inner; };
template<class T> template<class U> struct A<T>::Inner { int a; };
template<class T> template<class U> struct A<T>::Inner<const U*> { U u[3]; };
A<int>::Inner<const char*> iv;
A<int>::Inner<char> pv;
static_assert(sizeof(A<int>::Inner<const char*>) == 3, "");
static_assert(sizeof(A<long>::Inner<const char*>) == 3, "");
static_assert(sizeof(A<int>::Inner<char>) == 4, "");
