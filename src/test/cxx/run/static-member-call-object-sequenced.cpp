/* [expr.call]/8: "The postfix-expression is sequenced before each
   expression in the expression-list"; [expr.ref]/1: the object expression
   of a static member's call is evaluated (a comma's left operand,
   [over.match.funcs]/4: then discarded) before the arguments, a function
   template's or not; a null pointer is not dereferenced. */
int order[8], k;
struct S {
    template <class T> static int f(T t, T u) { return t * 10 + u; }
    static int g(int t, int u) { return t * 10 + u; }
    template <class T> static T operator()(T t) { return t; }
};
S *object()
{
    order[k++] = 1;
    return nullptr;
}
S value()
{
    order[k++] = 1;
    return S();
}
int argument()
{
    order[k++] = 2;
    return 3;
}
int main()
{
    int r = object()->f(argument(), 4);
    if (order[0] != 1 || order[1] != 2)
        return 1;
    k = 0;
    r += object()->g(argument(), 4);
    if (order[0] != 1 || order[1] != 2)
        return 2;
    k = 0;
    r += value().f(argument(), 5);
    if (order[0] != 1 || order[1] != 2)
        return 3;
    k = 0;
    r += value()(argument());
    if (order[0] != 1 || order[1] != 2)
        return 4;
    return r - (34 + 34 + 35 + 3);
}
