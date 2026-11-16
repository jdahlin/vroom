// [class.base.init] Example 3 (paragraph 6), docs/cxx-spec/class/class.base.init.md
// needs: P5
// status: annotated

struct C {
  C( int ) { }                  // #1: non-delegating constructor
  C(): C(42) { }                // #2: delegates to #1
  C( char c ) : C(42.0) { }     // #3: ill-formed due to recursion with #4 // semantic
  C( double d ) : C('a') { }    // #4: ill-formed due to recursion with #3 // semantic
};
