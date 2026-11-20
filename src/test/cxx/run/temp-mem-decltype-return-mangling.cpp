/* [temp.mem]/1, [dcl.type.decltype], [abi:mangling]: a member function template, static or not, whose trailing return type is decltype of an expression of its parameter: the specialization's symbol (the expression mangled, [abi:mangling.expr]) */
struct S {
    template <class T> static auto f(T t) -> decltype(t + 1) { return t + 1; }
    template <class T> auto g(T t) -> decltype(t + 1) { return t + 1; }
};
int main()
{
    S s;
    return S::f(1) - 2 + s.g(1) - 2;
}
