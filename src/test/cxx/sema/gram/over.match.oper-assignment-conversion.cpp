// [over.match.oper]/3.3, [over.built]: a class conversion cannot produce
// a built-in assignment candidate, whose left operand must be an lvalue.
struct X { operator bool() const; X& operator=(const X&); };
static_assert(!__is_assignable(const X&, const X&));
static_assert(__is_assignable(X&, const X&));
