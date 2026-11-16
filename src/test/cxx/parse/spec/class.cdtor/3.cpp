// [class.cdtor] Example 3 (paragraph 3), docs/cxx-spec/class/class.cdtor.md
// needs: P5
// status: well-formed (as far as the example says)

struct A { };
struct B : virtual A { };
struct C : B { };
struct D : virtual A { D(A*); };
struct X { X(A*); };

struct E : C, D, X {
  E() : D(this),    // undefined behavior: upcast from E* to A* might use path E* → D* → A*
                    // but D is not constructed

                    // ``D((C*)this)'' would be defined: E* → C* is defined because E() has started,
                    // and C* → A* is defined because C is fully constructed

  X(this) {}        // defined: upon construction of X, C/B/D/A sublattice is fully constructed
};
