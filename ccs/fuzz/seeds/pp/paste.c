#define cat(a,b) a##b
#define cat3(a,b,c) a##b##c
#define s(x) #x
#define xs(x) s(x)
cat(1,e) cat(<,<=) cat(L,'a') cat(u8,"s") cat(-,>) cat(%:,%:) cat(,) cat(a,)
xs(cat(a, b)) s("a\n" '\'' \ x) s(  a  "  "   b  ) xs(__LINE__)
cat3(a,,b) cat(x, cat)(1,2)
#define LPAREN (
#define f(x) [x]
#define g f LPAREN 1)
g
#define AA BB
#define BB AA
AA BB
#define rec(x) rec(x) x
rec(rec(1))
#define EMPTY
#define DEFER(id) id EMPTY
#define EXPAND(...) __VA_ARGS__
#define A() 123
DEFER(A)() EXPAND(DEFER(A)())
#define VA(fmt, ...) printf(fmt, ## __VA_ARGS__)
VA("x") VA("x",) VA("x", 1, 2)
#define NAMED(args...) named(args)
NAMED() NAMED(1,2)
#define str_va(...) #__VA_ARGS__
str_va() str_va( a , b ) str_va(,)
#define m1(x) x
m1(
#define inside 1
inside)
