// [class.static] Example 1 (paragraph 1), docs/cxx-spec/class/class.static.md
// needs: P5
// status: well-formed (as far as the example says)

struct process {
  static void reschedule();
};
process& g();

void f() {
  process::reschedule();        // OK: no object necessary
  g().reschedule();             // g() is called
}
