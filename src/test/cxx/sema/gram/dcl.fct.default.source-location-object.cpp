// [dcl.fct.default]/5: a default argument whose value is not made yet (__builtin_source_location()) leaves the function's defaults unknown, not absent: a call that omits it is not rejected ([over.match.viable]/2)
namespace std {
class source_location {
  struct __impl;
public:
  static constexpr int current(const __impl *p = __builtin_source_location()) noexcept { return 1; }
private:
  struct __impl { unsigned line; };
};
}
int x = std::source_location::current();
