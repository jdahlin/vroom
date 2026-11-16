// [class.static.data] Example 1 (paragraph 3), docs/cxx-spec/class/class.static.data.md
// needs: P5
// status: well-formed (as far as the example says)

class process {
  static process* run_chain;
  static process* running;
};

process* process::running = get_main();
process* process::run_chain = running;
