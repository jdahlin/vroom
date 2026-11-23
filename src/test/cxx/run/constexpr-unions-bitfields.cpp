/* Unions and bit-fields in the constant evaluator.  A union has one slot,
   its active member's value ([class.union]/2): zero-initialization and a
   list give the first or the designated member ([dcl.init]/6,
   [dcl.init.aggr]/5.2), an assignment makes another member active
   ([class.union]/6), a copy carries the active member.  A bit-field holds
   its value modulo 2^width, signed when its type is ([class.bit]/1). */
#include <stdio.h>

union U {
    int i;
    float f;
    char c;
};

union V {
    int a;
    long b;
    constexpr V(int x) : a(x) {}
};

union W {
    struct {
        int x;
        int y;
    } s;
    long l;
};

struct Tagged {
    int kind;
    union {
        int i;
        float f;
    };
};

struct Bits {
    int a : 3;
    unsigned b : 5;
    bool f : 1;
    signed char c : 4;
};

constexpr int write_read()
{
    U u{};
    u.i = 5;
    return u.i;
}

constexpr int zeroed()
{
    U u{};
    return u.i;
}

constexpr int switched()
{
    U u{};
    u.i = 5;
    u.c = 9;
    return u.c;
}

constexpr int copied()
{
    U u{};
    u.i = 5;
    U v = u;
    v.i = 6;
    return u.i * 10 + v.i;
}

constexpr int assigned()
{
    U u{};
    u.i = 5;
    U v{};
    v = u;
    u.i = 1;
    return v.i;
}

constexpr int through_constructor()
{
    V v(4);
    return v.a;
}

constexpr int struct_member()
{
    W w{};
    w.s.x = 3;
    w.s.y = 4;
    return w.s.x + w.s.y;
}

constexpr int anonymous()
{
    Tagged t{};
    t.kind = 1;
    t.i = 8;
    return t.kind + t.i;
}

constexpr int bit_wrap()
{
    Bits b{};
    b.a = 5;
    b.b = 37;
    b.c = 9;
    b.f = true;
    return b.a * 1000 + b.b * 100 + b.c * 10 + b.f;
}

constexpr int bit_arith()
{
    Bits b{};
    b.b = 31;
    ++b.b;
    b.a = 3;
    b.a += 1;
    return b.b * 10 + b.a;
}

constexpr U first_member{7};
constexpr U designated{.f = 2.0f};

static_assert(write_read() == 5);
static_assert(zeroed() == 0);
static_assert(switched() == 9);
static_assert(copied() == 56);
static_assert(assigned() == 5);
static_assert(through_constructor() == 4);
static_assert(struct_member() == 7);
static_assert(anonymous() == 9);
static_assert(bit_wrap() == -3 * 1000 + 5 * 100 + -7 * 10 + 1);
static_assert(bit_arith() == -4);
static_assert(first_member.i == 7);
static_assert(designated.f == 2.0f);

int main()
{
    printf("%d %d %d %d %d %d %d %d %d\n", write_read(), zeroed(), switched(),
           copied(), assigned(), struct_member(), anonymous(), bit_wrap(),
           bit_arith());
    return 0;
}
