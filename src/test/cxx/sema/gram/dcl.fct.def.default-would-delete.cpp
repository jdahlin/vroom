// [dcl.fct.def.default]/3: an explicitly defaulted function that would be
// implicitly deleted, defaulted after its first declaration, is ill-formed.
class A { A(); };
struct E : A { E(); virtual void foo() = 0; };
E::E() = default;
struct NoAccessDtor {
  NoAccessDtor(NoAccessDtor&&);
private:
  ~NoAccessDtor();
};
struct HasNoAccessDtor {
  NoAccessDtor NAD;
  HasNoAccessDtor(HasNoAccessDtor&&);
};
HasNoAccessDtor::HasNoAccessDtor(HasNoAccessDtor&&) = default;
struct Ok { int x; Ok(); Ok(const Ok&); };
Ok::Ok() = default;
Ok::Ok(const Ok&) = default;
