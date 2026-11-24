// [expr.new]/19, [expr.delete]/10: new-extended alignment passes std::align_val_t first.
namespace std { enum class align_val_t : unsigned long {}; typedef decltype(sizeof(0)) size_t; }
typedef decltype(sizeof(0)) size_t;
void *operator new(size_t, std::align_val_t);
void operator delete(void *, std::align_val_t) noexcept;
void *operator new(size_t);
void operator delete(void *) noexcept;

struct alignas(64) A { int x; };
struct B { int x; };
struct alignas(32) C {
  static void *operator new(size_t, std::align_val_t);
  static void operator delete(void *, std::align_val_t);
};
struct alignas(32) D {
  static void *operator new(size_t);   // no aligned form here: plain
};
struct alignas(32) E {
  template <typename T> static void *operator new(size_t n, T);
};
A *f() { return new A; }
void g(A *a) { delete a; }
B *h() { return new B; }
C *i() { return new C; }
void j(C *c) { delete c; }
D *k() { return new D; }
E *l() { return new E; }
