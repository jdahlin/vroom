// [class.prop] Example 2 (paragraph 7), docs/cxx-spec/class/class.prop.md
// needs: P5
// status: well-formed (as far as the example says)

struct N {          // neither trivial nor standard-layout
  int i;
  int j;
  virtual ~N();
};

struct T {          // trivial but not standard-layout
  int i;
private:
  int j;
};

struct SL {         // standard-layout but not trivial
  int i;
  int j;
  ~SL();
};

struct POD {        // both trivial and standard-layout
  int i;
  int j;
};
