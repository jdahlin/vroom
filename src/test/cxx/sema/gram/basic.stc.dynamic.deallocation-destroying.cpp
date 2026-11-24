// [basic.stc.dynamic.deallocation]/2, /3: a destroying operator delete is a
// member function of C whose first parameter is C*; at namespace scope it is
// just a placement delete
namespace std {
struct destroying_delete_t {
  explicit destroying_delete_t() = default;
};
}
struct A {
  void operator delete(A *, std::destroying_delete_t);
};
struct B {
  void operator delete(struct X *, std::destroying_delete_t); // expected-error {{1st parameter of destroying 'operator delete' must have type 'B *'}}
};
void operator delete(void *, std::destroying_delete_t);
void f(A *a) { delete a; }
