// [temp.deduct.call]/1: a braced-init-list argument is a non-deduced
// context unless P is std::initializer_list<P'> or P'[N]; then P' is
// deduced from each element.
namespace std { template <class T> struct initializer_list { const T *b; unsigned long n; initializer_list(const T*, unsigned long); }; }
struct S { int a, b; };
template <class T> T first(T x, S s) { return x; }
template <class T> int count(std::initializer_list<T> l) { return 1; }
template <class T> void both(T, T);
int main() {
  int r = first(1, {2, 3});
  count({1, 2, 3});
  both(1, {2});
}
