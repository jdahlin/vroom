// [over.call.object]/2: function-pointer conversion competes with the
// function call operator, while an argument's conversions do not.
struct Arg { operator int() const; };
struct F { int operator()(Arg) const; explicit operator bool() const; };
int ordinary(F f, Arg a) { return f(a); }
using Fn = int (*)(int);
struct G { operator Fn() const; };
int surrogate(G g) { return g(1); }
