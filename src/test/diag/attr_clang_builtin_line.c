/* diag-flags: -std=c23 */
[[__clang__::noinline]] void f(void) {}
int main(void) { f(); return 0; }
