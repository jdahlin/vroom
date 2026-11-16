// [stmt.if] Example 1 (paragraph 2), docs/cxx-spec/stmt/stmt.if.md
// needs: P7
// status: well-formed (as far as the example says)

template<typename T, typename ... Rest> void g(T&& p, Rest&& ...rs) {
  // ... handle p

  if constexpr (sizeof...(rs) > 0)
    g(rs...);       // never instantiated with an empty argument list
}

extern int x;       // no definition of x required

int f() {
  if constexpr (true)
    return 0;
  else if (x)
    return x;
  else
    return -x;
}
