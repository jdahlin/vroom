// [dcl.ambig.res] Example 2 (paragraph 2), docs/cxx-spec/dcl/dcl.ambig.res.md
// needs: P7
// status: annotated

template <class T> struct X {};
template <int N> struct Y {};
X<int()> a;                     // type-id
X<int(1)> b;                    // expression (ill-formed) // semantic
Y<int()> c;                     // type-id (ill-formed) // semantic
Y<int(1)> d;                    // expression

void foo(signed char a) {
  sizeof(int());                // type-id (ill-formed) // semantic
  sizeof(int(a));               // expression
  sizeof(int(unsigned(a)));     // type-id (ill-formed) // semantic

  (int())+1;                    // type-id (ill-formed) // semantic
  (int(a))+1;                   // expression
  (int(unsigned(a)))+1;         // type-id (ill-formed) // semantic
}
