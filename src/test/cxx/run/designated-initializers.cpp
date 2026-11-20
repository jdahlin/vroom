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
static int local()
{
    int v = 3;
    S l = {.b = v, .a = v + 1, .s = "l"};
    int a[5] = {[4] = v, [1 ... 2] = v + 1};
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
    printf("%d\n", local());
    return 0;
}
