// [temp.explicit]/3: an explicit instantiation of a member class of a class template's specialization
template <class T> struct A {
  struct B { T v; int f() { return 1; } };
};
template struct A<int>::B;
extern template struct A<char>::B;
