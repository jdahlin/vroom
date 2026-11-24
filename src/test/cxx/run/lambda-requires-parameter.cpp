/* [expr.prim.lambda]/1, [basic.scope.param]/1: a generic lambda's
   requires-clause after its parameters names them.  (Only unevaluated
   uses: mangling a requires-expression is not done yet.) */
template <class T> concept boolean_testable = true;

inline constexpr auto less_synth = []<class A, class B>(const A &a, const B &b) -> int
    requires requires {
        { a < b } -> boolean_testable;
        { b < a } -> boolean_testable;
    }
{
    if (a < b)
        return -1;
    if (b < a)
        return 1;
    return 0;
};

static_assert(sizeof(decltype(less_synth(1, 2))) == sizeof(int));

int main()
{
    return 0;
}
