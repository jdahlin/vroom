// [ext:gnu_typeof] typeof, __typeof and __typeof__ as a type-specifier with
// an expression or a type-id operand, where a declaration, a cast, a
// parameter or a template argument has one; (T()) is an expression, T() a
// type-id.
typedef int T;
int g;
int a[3];
int f(double);
__typeof(g) v1;
__typeof__(g) v2;
__typeof(T) v3;
__typeof(int *) v4;
__typeof(T()) *v5;
__typeof((T())) v6;
__typeof(a[1]) v7;
__typeof(f(1)) v8;
__typeof(g + 1.5) v9;
const __typeof(g) v10 = 0;
__typeof(g) const *v11;
typedef __typeof(g) T2;
T2 v12;
void h(__typeof(g) x, __typeof(int) y);
template <class U> struct S {
    __typeof(sizeof(U)) size;
    static U make() { return (__typeof(U()))(U()); }
};
int k = (int)(__typeof(g))(g);
auto fn() -> __typeof(g) { return g; }
