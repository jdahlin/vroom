// [over.ics.list]/5, /8: f({{1,2},{3,4}}) for std::initializer_list<int[2]>
// and <int[2][2]>: an element {1,2} converts to int[2] but not to int[2][2]
// (each of its elements would have to convert to int[2]; brace elision is
// the initialization's, not the conversion sequence's), so the first is the
// only viable function and the deleted second one is not chosen.
namespace std {
template <class T> class initializer_list {
  const T *b;
  decltype(sizeof 0) n;
public:
  initializer_list(const T *, decltype(sizeof 0));
};
}
void f(std::initializer_list<int[2]>) {}
void f(std::initializer_list<int[2][2]>) = delete;
void h() { f({{1, 2}, {3, 4}}); }
