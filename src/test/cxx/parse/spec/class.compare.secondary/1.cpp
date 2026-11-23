// [class.compare.secondary] Example 1 (paragraph 3), docs/cxx-spec/class/class.compare.secondary.md
// needs: P5
// status: well-formed (as far as the example says)

struct HasNoLessThan { };

struct C {
  friend HasNoLessThan operator<=>(const C&, const C&);
  bool operator<(const C&) const = default;             // OK, function is deleted
};
