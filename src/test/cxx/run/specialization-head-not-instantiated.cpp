/* cxx-run-flags: -std=c++20 */
/* [temp.inst]/2: a class template specialization named in a partial or
   explicit specialization's head needs no completely-defined type, so it is
   not instantiated there: plus<void>'s members would declare a reference to
   void ([dcl.ref]/1), and its explicit specialization follows the use
   ([temp.expl.spec]/7; libc++'s __desugars_to_v before plus<void>).  Objects,
   sizeof, member access, base classes and calls still instantiate. */
template <class T = void> struct plus { T g(const T& x) const { return x; } int k = 4; };
template <class A, class B> constexpr bool d = false;
template <class T> constexpr bool d<plus<void>, T> = true;
template <> constexpr bool d<plus<int>, int> = true;
template <> struct plus<void> { int k = 7; };
struct B : plus<long> {};
int main() {
  plus<void> v; plus<int> i; B b;
  return (v.k == 7 && i.g(3) == 3 && sizeof(plus<char>) == 4 && b.k == 4 && d<plus<>, int> && d<plus<int>, int>) ? 0 : 1;
}
