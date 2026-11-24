// [dcl.spec.auto]/12, [dcl.type.decltype]/1.3: decltype(auto) of a returned unparenthesized name is its declared type
template <class T, class U> constexpr bool is_same_v = false;
template <class T> constexpr bool is_same_v<T, T> = true;
decltype(auto) f1(auto x) { return x; }
static_assert(is_same_v<decltype(f1(1)), int>);
template <class T> decltype(auto) f2(T x) { return x; }
static_assert(is_same_v<decltype(f2('c')), char>);
