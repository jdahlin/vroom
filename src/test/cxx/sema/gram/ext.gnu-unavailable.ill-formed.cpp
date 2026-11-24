// [ext.gnu] clang's unavailable attribute: a use of the function or variable
// is an error, except inside an unavailable function or a member of an
// unavailable class.
int old_function(int) __attribute__((unavailable("replaced"))); // expected-note {{explicitly marked unavailable}}
int old_variable __attribute__((unavailable)); // expected-note {{explicitly marked unavailable}}
int fine() __attribute__((unavailable)) { return old_function(1) + old_variable; }
struct __attribute__((unavailable)) Gone {
  int member() { return old_function(2); }
};
int user() {
  return old_function(3); // expected-error {{'old_function' is unavailable: replaced}}
}
int user2() {
  return old_variable; // expected-error {{'old_variable' is unavailable}}
}
