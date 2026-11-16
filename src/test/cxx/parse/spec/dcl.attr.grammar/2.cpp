// [dcl.attr.grammar] Example 2 (paragraph 7), docs/cxx-spec/dcl/dcl.attr.grammar.md
// needs: P4
// status: annotated

int p[10];
void f() {
  int x = 42, y[5];
  int(p[[x] { return x; }()]);  // error: invalid attribute on a nested declarator-id and // syntax-error
                                // not a function-style cast of an element of p.
  y[[] { return 2; }()] = 2;    // error even though attributes are not allowed in this context. // syntax-error
  int i [[vendor::attr([[]])]]; // well-formed implementation-defined attribute.
}
