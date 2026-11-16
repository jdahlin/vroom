// [dcl.attr.likelihood] Example 1 (paragraph 3), docs/cxx-spec/dcl/dcl.attr.likelihood.md
// needs: P4
// status: annotated

void g(int);
int f(int n) {
  if (n > 5) [[unlikely]] {     // n > 5 is considered to be arbitrarily unlikely
    g(0);
    return n * 2 + 1;
  }

  switch (n) {
  case 1:
    g(1);
    [[fallthrough]];

  [[likely]] case 2:            // n == 2 is considered to be arbitrarily more
    g(2);                       // likely than any other value of n
    break;
  }
  return 3;
}
