// [basic.def.odr]/8, [temp.inst]/4: a default argument that converts to a class template's
// specialization needs that specialization's constructor (and destructor) in the call that uses
// it (json's parse(InputType&&, parser_callback_t cb = nullptr), a std::function)
template <class T> struct fn {
  int k;
  fn(decltype(nullptr)) : k(7) {}
  fn(int i) : k(i) {}
  ~fn() { k = 0; }
};
int f(int i, fn<int> c = nullptr) { return c.k + i; }
struct bj {
  int v = 0;
  template <class In> static bj parse(In &&i, fn<long> cb = nullptr, bool e = true) {
    bj r;
    r.v = sizeof(i) + cb.k + e;
    return r;
  }
};
struct holder {
  fn<char> c;
  holder(fn<char> x = fn<char>(3)) : c(x) {}
};
int main() {
  holder h;
  return f(0) == 7 && bj::parse("abc").v == 12 && h.c.k == 3 ? 0 : 1;
}
