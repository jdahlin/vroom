// X::~X() = default; outside the class (doctest's DOCTEST_DEFINE_INTERFACE): the key function, its body and the
// vtable defined here; a class template derived from it holding a lambda (doctest's ContextScope)
struct IC { virtual ~IC(); IC() = default; IC(const IC &) = delete; IC(IC &&) = delete; IC &operator=(const IC &) = delete; IC &operator=(IC &&) = delete; virtual void st(int *) const = 0; };
IC::~IC() = default;
struct CB : public IC {
  CB(const CB &) = delete; CB &operator=(const CB &) = delete; CB &operator=(CB &&) = delete;
  ~CB() override = default;
protected:
  CB() {} CB(CB &&o) noexcept : need(o.need) { o.need = false; }
  void destroy() { (*cnt)++; } bool need{true}; static int *cnt;
};
int g = 0; int *CB::cnt = &g;
template <class L> class CS : public CB {
  L l;
public:
  explicit CS(const L &x) : l(x) {}
  CS(CS &&) noexcept = default;
  void st(int *s) const override { l(s); }
  ~CS() override { if (need) destroy(); }
};
template <class L> CS<L> make(const L &x) { return CS<L>(x); }
int main() { int k = 0; { auto c = make([&](int *p) { *p = k; }); int q = 1; c.st(&q); } return g == 1 ? 0 : 1; }
