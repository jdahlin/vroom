/* [dcl.init.aggr]/3.1 and [ext:gnu_designated_initializers]: designators of C++20 (".a = x", ".a{x}"), of C99 and GNU ("[i] =", "[i ... j] =", ".a.b =", "a:") */
extern "C" int printf(const char *, ...);
struct S { int a; int b; const char *s; };
struct N { S in; int z[3]; int w; };
struct D { int a = 4; int b = 5; int c; };
struct U { int x; float f; };
struct P { int *p; int n; };
static int g;

/* C++20: ".a = x" and ".a{x}", in declaration order, some left out */
static const S s1 = {.a = 1, .s = "x"};
static S s2 = {.a = 2, .b{3}};
static const S table[] = {{.b = 2}, {.a = 3, .b = 4}, {.s = "z"}};
static const D d1 = {.b = 9};
static const D d2 = {.c = 1};
static const U u1 = {.x = 6};
static const P p1 = {.p = &g};
/* clang accepts these as extensions, with warnings */
static const S s3 = {.b = 5, .a = 1};
static const S s4 = {.a = 1, 7, "mixed"};
static const S s5 = {.a = 1, .a = 2};
static const N n1 = {.in = {.a = 1}, .z = {1, 2}};
static const N n2 = {.in.b = 3, .z[1] = 4, .w = 5};
static const N n3 = {.in = 5, 6, "q", 7};
static const S s6 = {a: 8, b: 9};
/* C99 arrays */
static const char cls[16] = {['0' - '0' + 1 ... 3] = 1, [5] = 2, [12 ... 13] = 3};
static const int arr1[] = {[2] = 5, [0] = 1, 7};
static const int arr2[] = {[1] = 1, 2, 3};
static const S arr3[4] = {[1] = {.a = 1}, [3] = {.b = 3}};
static const int arr4[2][3] = {[1][2] = 6, [0] = {1, 2}};
static const S *const ptrs[] = {[1] = &s1, [0] = &s2};

struct C { char name[6]; int v[2]; };
static const C cs[3] = {[2].name = "abc", [2].v[1] = 7, [0] = {.v = {1, 2}}};
static const U u2 = {.x = 9};
/* a member of an anonymous union or structure: designated through it */
struct R { unsigned char op, size; union { long imm; struct { int off; unsigned ref; }; unsigned lab; }; int tail; };
struct Q { int head; struct { int x; int y; }; int tail; };
static const R r1 = {.op = 1, .size = 2, .imm = -16};
static const R r2 = {.imm = 5, .tail = 9};
static const Q q1 = {.head = 1, .x = 2, .y = 3, .tail = 4};
static const Q q2 = {.y = 7};
/* a union member but the first */
union W { int i; float f; char c[8]; };
struct V { int tag; W w; int tail; };
static const W w1 = {.f = 1.5f};
static const V v1 = {.tag = 1, .w = {.c = {5, 6}}, .tail = 2};
static const V v2 = {.w.f = 2.5f};
static const R r3 = {.op = 2, .off = -3, .tail = 4};
static const R r4 = {.size = 1, .lab = 77};
static int local()
{
    int v = 3;
    S l = {.b = v, .a = v + 1, .s = "l"};
    int a[5] = {[4] = v, [1 ... 2] = 4};
    N n = {.z[2] = v, .in = {.b = 8}};
    return l.a * 10000 + l.b * 1000 + a[4] * 100 + a[1] * 10 + a[2] + n.z[2] + n.in.b;
}

int main()
{
    printf("%d %d %d %d %s\n", s1.a, s1.b, s1.s != 0, s1.s[0], s1.s);
    printf("%d %d %d\n", s2.a, s2.b, s2.s == 0);
    printf("%zu %d %d %d %d %d\n", sizeof table / sizeof table[0], table[0].b, table[1].a, table[1].b, table[2].a, table[2].s[0]);
    printf("%d %d %d %d %d %d\n", d1.a, d1.b, d1.c, d2.a, d2.b, d2.c);
    printf("%d %f %d %d\n", u1.x, u1.f, p1.p == &g, p1.n);
    printf("%d %d %d %d %d %s %d %d\n", s3.a, s3.b, s4.a, s4.b, s5.a, s4.s, s5.b, s5.s == 0);
    printf("%d %d %d %d %d %d\n", n1.in.a, n1.in.b, n1.z[0], n1.z[1], n1.z[2], n1.w);
    printf("%d %d %d %d %d %d\n", n2.in.a, n2.in.b, n2.z[0], n2.z[1], n2.z[2], n2.w);
    printf("%d %d %s %d %d %d %d\n", n3.in.a, n3.in.b, n3.in.s, n3.z[0], n3.z[1], n3.z[2], n3.w);
    printf("%d %d\n", s6.a, s6.b);
    for (int i = 0; i < 16; i++)
        printf("%d", cls[i]);
    printf("\n%zu %d %d %d\n", sizeof arr1 / sizeof arr1[0], arr1[0], arr1[1], arr1[2]);
    printf("%zu %d %d %d\n", sizeof arr2 / sizeof arr2[0], arr2[0], arr2[1], arr2[2]);
    printf("%d %d %d %d\n", arr3[0].a, arr3[1].a, arr3[3].b, arr3[2].b);
    printf("%d %d %d %d\n", arr4[0][0], arr4[0][1], arr4[1][0], arr4[1][2]);
    printf("%d %d\n", ptrs[0] == &s2, ptrs[1] == &s1);
    printf("%s %d %d %d %d %d\n", cs[2].name, cs[2].v[0], cs[2].v[1], cs[0].v[1], cs[0].name[0], cs[1].v[0]);
    printf("%d\n", u2.x);
    printf("%d %d %ld %d %d %d %d %d %d %d\n", r1.op, r1.size, r1.imm, r1.tail, r2.op, (int)r2.imm, r2.tail, q1.head + q1.x * 10 + q1.y * 100 + q1.tail * 1000, q2.x, q2.y);
    printf("%f %d %d %d %f\n", w1.f, v1.tag, v1.w.c[1], v1.tail, v2.w.f);
    printf("%d %d %d %d %d %u\n", r3.op, r3.off, (int)r3.ref, r3.tail, r4.size, r4.lab);
    W wl = {.f = (float)local()};
    printf("%f\n", wl.f);
    R rl = {.size = 3, .imm = local() - 1};
    printf("%d %ld\n", rl.size, rl.imm);
    printf("%d\n", local());
    return 0;
}
