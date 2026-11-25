// RUN: %clang_cc1 -std=c++23 %s
// P1938R3 (C++23), [stmt.if]/1: if !_opt consteval compound-statement
// (else compound-statement)_opt.  No parentheses, and both substatements are
// compound-statements (an `else if consteval` is ill-formed).
consteval int twice(int i) { return 2 * i; }
constexpr int pick(int i) {
  if consteval {
    return twice(i);
  } else {
    return i;
  }
}
constexpr int negated(int i) {
  if !consteval {
    return i;
  }
  return 0;
}
constexpr int chain(int i) {
  if consteval {
    return 1;
  } else {
    if !consteval {
      return 2;
    }
  }
  return i;
}
static_assert(negated(1) == 0 && chain(5) == 1);
