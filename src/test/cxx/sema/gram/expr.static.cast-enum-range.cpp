// [expr.static.cast]/10: a value outside an enumeration's range
// ([dcl.enum]/8), its underlying type not fixed, is undefined behavior:
// no constant expression, also when only the evaluator sees the value
enum E { a, b, c };
constexpr E f(int i) { return static_cast<E>(i); }
constexpr E in_range = f(3);
constexpr E out_of_range = f(99);
constexpr E direct = static_cast<E>(4);
enum F : int { x };
constexpr F fixed = static_cast<F>(99);
