// [dcl.ambig.res]/2: a type-id where the tokens can be one; and the
// contexts whose expressions P1 and P2 kept as tokens.
typedef int I;
constexpr int v = 3;
struct S { int m; };

int a1[sizeof(I())];            // type-id: a function type, ill-formed for sizeof // semantic
int a2[sizeof(I(v))];           // expression
int a3[sizeof(I) + sizeof(v)];
int a4[(I)v + 1][v ? 2 : 3];
alignas(I) int b1;
alignas(I *) int b2;
alignas(v + 1) int b3;
alignas(sizeof(I)) int b4;
alignas(alignof(S)) int b5;
decltype(v) c1 = 0;
decltype((v)) c2 = v;
decltype(v + 1) c3 = 0;
decltype(S().m) c4 = 0;
decltype(sizeof(I)) c5 = 0;
enum E : decltype(v + 0) { e1 = sizeof(I), e2 = (I)e1 + 1, e3 = e1 < e2, e4 = e3 ? e1 : e2 };
void f1() noexcept(sizeof(I) == 4);
void f2() noexcept(noexcept(I()));
void f3() noexcept(v > 2 && true);
void f4(int = sizeof(I), int = (I)v, int (*)(I) = 0, int[v + 1] = nullptr);
auto f5() -> decltype(v + 1);
auto f6(int x) -> decltype(x + v);
static_assert(sizeof(S) == sizeof(I), "one member");
static_assert((I)v == v);
static_assert(true ? true : false, "conditional");
int (*g1)(I) = nullptr;
int g2(I(v));                    // a variable: v is no type
int g3((I)v);
int g4(I(*)(I));                 // a function: I(*)(I) is a type-id
