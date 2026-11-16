// [class.access.base] Example 1 (paragraph 2), docs/cxx-spec/class/class.access.base.md
// needs: P5
// status: well-formed (as far as the example says)

class B { /* ... */ };
class D1 : private B { /* ... */ };
class D2 : public B { /* ... */ };
class D3 : B { /* ... */ };             // B private by default
struct D4 : public B { /* ... */ };
struct D5 : private B { /* ... */ };
struct D6 : B { /* ... */ };            // B public by default
class D7 : protected B { /* ... */ };
struct D8 : protected B { /* ... */ };
