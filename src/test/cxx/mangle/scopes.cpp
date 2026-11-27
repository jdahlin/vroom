/* [abi:mangling-scope], [abi:mangling.named], [abi:closure-types] */
inline int local_statics()
{
    static int x = 1;
    {
        static int x = 2;
        (void)x;
    }
    return x;
}
inline int lambdas()
{
    auto a = [](int i) { return i; };
    auto b = [](int i) { return i + 1; };
    auto c = [](char) { return 0; };
    return a(1) + b(2) + c('c');
}
inline int local_class()
{
    struct L {
        int f() { return 1; }
    };
    return L().f();
}
struct WithUnnamed {
    struct {
        int g() { return 2; }
    } member;
};
int use_all()
{
    WithUnnamed w;
    return local_statics() + lambdas() + local_class() + w.member.g();
}
// MANGLE: _ZZ13local_staticsvE1x
// MANGLE: _ZZ13local_staticsvE1x_0
// MANGLE: _ZZ7lambdasvENKUliE_clEi
// MANGLE: _ZZ7lambdasvENKUliE0_clEi
// MANGLE: _ZZ7lambdasvENKUlcE_clEc
// MANGLE: _ZZ11local_classvEN1L1fEv
// MANGLE: _ZN11WithUnnamedUt_1gEv
