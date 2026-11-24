// [class.union.anon]/1: "Each member-declaration in the member-specification of an anonymous union shall either
// define a non-static data member or be a static_assert-declaration".
struct S {
  union {
    static const int n; // expected-error {{static data member 'n' not allowed in anonymous union}}
    int a;
  };
};
