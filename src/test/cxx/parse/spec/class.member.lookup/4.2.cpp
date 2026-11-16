// [class.member.lookup] Example 4, part 2 (paragraph 10), docs/cxx-spec/class/class.member.lookup.md
// needs: P5 (names declared in another part or outside the example stay unparsed)
// status: annotated

void D::glorp() {
  x++;              // OK: B::x hides V::x
  f();              // OK: B::f() hides V::f()
  y++;              // error: B::y and C's W::y // semantic
  g();              // error: B::g() and C's W::g() // semantic
}
