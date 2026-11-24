// [temp.explicit]/3: an explicit instantiation of a class named by a qualified simple-template-id
namespace N {
template <class T> struct B { T v; int f() { return 1; } };
}
template struct N::B<float>;
extern template class N::B<int>;
struct Outer {
  template <class T> struct In { T v; };
};
template struct Outer::In<char>;
