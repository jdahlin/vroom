// [expr.unary], [expr.unary.op], [expr.sizeof], [expr.alignof],
// [expr.unary.noexcept], [expr.cast], [dcl.ambig.res]/2.
constexpr int a = 7;
constexpr const int *p = &a;
struct T { char c[3]; };
typedef int I;
void g() noexcept;
void h();

static_assert(-a + +a == 0);
static_assert(!a == 0);
static_assert(~a + 1 == -a);
static_assert(*p + *&a == 14);
static_assert(- -a == 7);
static_assert(!!a);
static_assert(-~a == 8);

static_assert(sizeof a + 1 == sizeof(int) + 1);
static_assert(sizeof(T) * 2 == 6);
static_assert(sizeof(a) + sizeof(I) == 2 * sizeof(int));
static_assert(sizeof -a == sizeof(int));
static_assert(sizeof(char) + sizeof(char[4]) == 5);
static_assert(sizeof (a) + 1 == 5);
static_assert(sizeof sizeof a == sizeof(decltype(sizeof 0)));
static_assert(alignof(char) == 1);
static_assert(alignof(T[2]) + alignof(char) == 2);
static_assert(noexcept(g()) && !noexcept(h()));
static_assert(noexcept(a + 1));

// ( type-id ) cast-expression against a parenthesized expression
static_assert((int)-1 == -1);
static_assert((I)-1 == -1);
static_assert((a)-1 == 6);
static_assert((I)+a == 7);
static_assert((a)+a == 14);
static_assert((I)(a) == 7);
static_assert((char)(I)-1 == -1);
static_assert((I)sizeof(I) == 4);
static_assert((I)~a == -8);
static_assert((I)!a == 0);
static_assert((const I)a == 7);
static_assert((I)a * 2 == 14);
static_assert((a) * 2 == 14);
static_assert(*(const int *)p == 7);
static_assert((I(a)) + 1 == 8);
static_assert((I(1)) == 1);
static_assert(sizeof(I(a)) == sizeof(int));
static_assert(sizeof(int[3]) == 12);
static_assert(sizeof(int(*)[3]) == sizeof(void *));
static_assert(sizeof(int(*)(I)) == sizeof(void *));
