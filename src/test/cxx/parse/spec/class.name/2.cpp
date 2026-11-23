// [class.name] Example 2 (paragraph 2), docs/cxx-spec/class/class.name.md
// needs: P5
// status: well-formed (as far as the example says)

struct stat {
  // ...
};

stat gstat;                     // use plain stat to define variable

int stat(struct stat*);         // redeclare stat as function

void f() {
  struct stat* ps;              // struct prefix needed to name struct stat
  stat(ps);                     // call stat()
}
