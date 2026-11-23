// [class.mi] Example 2 (paragraph 3), docs/cxx-spec/class/class.mi.md
// needs: P5
// status: annotated

class X { /* ... */ };
class Y : public X, public X { /* ... */ };             // error // semantic
