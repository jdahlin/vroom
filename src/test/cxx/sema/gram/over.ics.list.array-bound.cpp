// RUN: %clang_cc1 -std=c++11 -fsyntax-only %s
// [over.ics.list]/6: a braced-init-list to an array of N X (a reference to
// const array binding a temporary, /9): no sequence when the list has more
// than N elements, else the worst element conversion (CWG1307)
void f(int const (&)[2]);
void f(int const (&)[3]);
void three() { f({1, 2, 3}); }
void g(int const (&)[2]);
void too_many() { g({1, 2, 3}); } // expected-error {{no matching function}} expected-note@8 {{candidate function not viable}}
void h(int const (&)[3]);
void h(long);
void list_or_scalar() { h({1, 2, 3}); }
