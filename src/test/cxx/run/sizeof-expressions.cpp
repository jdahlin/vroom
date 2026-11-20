/* [expr.sizeof]/1: the operand is an expression, an unevaluated operand
   ([expr.prop]); /2: a reference's is its referenced type's; /3: no
   lvalue-to-rvalue, array-to-pointer or function-to-pointer conversion;
   [expr.prim.id]/3: a non-static data member named in an unevaluated
   operand (sizeof(S::m), and sizeof(m) in the class) */
struct S {
    int m;
    char buf[10];
    static int sm;
    int a = sizeof(m);
    char b[sizeof(m) + sizeof(buf)];
    int get() const { return sizeof(m) + sizeof(this->buf) + sizeof(*this); }
    static constexpr int n = sizeof(S::buf);
};
int side;
int f() { side = 1; return 0; }
double g();
int (&ref())[7];
long long &lref();
template <class T> struct W { T t; };
int array[6];
int *p = array;

static_assert(sizeof(S::m) == 4 && sizeof(S::buf) == 10 && sizeof(S::sm) == 4);
static_assert(sizeof(S::buf[0]) == 1);
static_assert(sizeof(((S *)0)->m) == 4);
static_assert(sizeof(S{}.m) == 4);
static_assert(S::n == 10);
static_assert(sizeof(S) == 36);
static_assert(sizeof(f()) == 4 && sizeof(g()) == 8);
static_assert(sizeof(ref()) == 28 && sizeof(lref()) == 8);
static_assert(sizeof array == 24 && sizeof(array) == 24 && sizeof(+array) == 8);
static_assert(sizeof p == 8 && sizeof *p == 4 && sizeof(p[1]) == 4);
static_assert(sizeof("abc") == 4 && sizeof(u"abc") == 8 && sizeof(L"a") == 8);
static_assert(sizeof('a') == 1 && sizeof(1) == 4 && sizeof(1L) == 8);
static_assert(sizeof(1.0f) == 4 && sizeof(1.0) == 8 && sizeof(true) == 1);
static_assert(sizeof(nullptr) == 8);
static_assert(sizeof(1 + 1L) == 8 && sizeof(1 < 2) == 1 && sizeof(&side) == 8);
static_assert(sizeof(sizeof 1) == 8);
static_assert(sizeof(&S::m) == 8 && sizeof(&S::get) == 16);
static_assert(sizeof(W<int>{}) == 4 && sizeof(W<char>) == 1);
static_assert(sizeof sizeof(int) == 8);
static_assert(sizeof(int) * 2 == 8 && sizeof(int) - 1 == 3);
static_assert(alignof(S) == 4);
int arr2[sizeof(S::m) * 2];
auto lam = [](int x) { return x; };
static_assert(sizeof(lam(1)) == 4);
static_assert(sizeof(decltype(lam)) == 1);
constexpr int cv = 3;
static_assert(sizeof(cv) == 4);
enum { EN = sizeof(long) };
static_assert(EN == 8);
int main()
{
    int one = 1;
    S s;
    /* unevaluated: no side effect */
    unsigned long a = sizeof(f());
    unsigned long b = sizeof(side = 5);
    unsigned long c = sizeof(one++);
    unsigned long d = sizeof(s.get());
    if (side != 0 || one != 1 || a != 4 || b != 4 || c != 4 || d != 4)
        return 1;
    if (sizeof s.m != 4 || sizeof(s.buf) != 10 || arr2[1] != 0 || sizeof arr2 != 32)
        return 2;
    if (s.get() != 4 + 10 + sizeof(S))
        return 3;
    return 0;
}
