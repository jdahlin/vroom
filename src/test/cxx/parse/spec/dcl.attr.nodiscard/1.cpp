// [dcl.attr.nodiscard] Example 1 (paragraph 5), docs/cxx-spec/dcl/dcl.attr.nodiscard.md
// needs: P5
// status: annotated

struct [[nodiscard]] my_scopeguard { /* ... */ };
struct my_unique {
  my_unique() = default;                                // does not acquire resource
  [[nodiscard]] my_unique(int fd) { /* ... */ }         // acquires resource
  ~my_unique() noexcept { /* ... */ }                   // releases resource, if any
  /* ... */
};
struct [[nodiscard]] error_info { /* ... */ };
error_info enable_missile_safety_mode();
void launch_missiles();
void test_missiles() {
  my_scopeguard();              // warning encouraged
  (void)my_scopeguard(),        // warning not encouraged, cast to void
    launch_missiles();          // comma operator, statement continues
  my_unique(42);                // warning encouraged
  my_unique();                  // warning not encouraged
  enable_missile_safety_mode(); // warning encouraged
  launch_missiles();
}
error_info &foo();
void f() { foo(); }             // warning not encouraged: not a nodiscard call, because neither
                                // the (reference) return type nor the function is declared nodiscard
