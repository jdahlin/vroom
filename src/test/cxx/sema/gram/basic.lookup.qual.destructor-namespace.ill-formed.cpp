// [basic.lookup.qual]/6: a qualified destructor name is
// nested-name-specifier type-name :: ~ type-name; a namespace before :: ~
// names no type, and a namespace has no destructor
namespace N { struct B {}; }
void f(N::B *p) {
  p->N::B::~B();
  p->N::~B(); // expected-error {{no member named '~B' in namespace 'N'}}
}
