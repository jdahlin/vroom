/* [abi:mangling.named]: classes and enumerations with no name of namespace
   and class scope as template arguments; those of namespace scope have no
   linkage ([basic.link]/4): [impl.clang] $_N, the specialization local */
template <class T> int f(T) { return 1; }

enum { namespace_value };
struct {
    int i;
} namespace_object;
struct S {
    enum { m1 };
    struct {
        int i;
    } u;
    enum { m2 };
};

int local_first()
{
    enum { local };
    return f(local);
}

int main()
{
    S s;
    return local_first() - 1 + f(namespace_value) + f(namespace_object) + f(S::m1) + f(s.u) +
           f(S::m2) - 5;
}
