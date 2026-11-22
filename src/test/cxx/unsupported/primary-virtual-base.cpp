/* cxx-unsupported: lowering */
// [abi:class-types] I: a nearly empty virtual base is the primary base of B
// and shares its virtual table pointer: the analysis lays B out as clang
// does, the code generator has no rules for it yet.
struct A {
    virtual void f();
};
struct B : virtual A {
    int b;
};
B *make()
{
    return new B;
}
