// [abi:vtable-components]: a class with many dynamic bases, its secondary
// virtual tables copied from each base's groups (vtable_groups grows while
// a base's group is read), each slot the class overrides called through a
// this-adjusting thunk ([class.virtual]/2)
#include <stdio.h>

struct B0 { int v0; virtual int f0() { return 0; } };
struct B1 { int v1; virtual int f1() { return 1; } };
struct B2 { int v2; virtual int f2() { return 2; } };
struct B3 { int v3; virtual int f3() { return 3; } };
struct M1 : B0, B1, B2, B3 { int f1() { return 11; } int f3() { return 13; } };
struct M2 : B1, B2, B3, B0 { int f2() { return 22; } int f0() { return 20; } };
struct M3 : B3, B2, B1, B0 { int f1() { return 31; } };
/* more groups before D's, so that the array grows while D's are copied */
struct E1 : B0 { int f0() { return 41; } };
struct E2 : B1 { int f1() { return 42; } };
struct E3 : B2 { int f2() { return 43; } };
struct E4 : B3 { int f3() { return 44; } };
struct E5 : B0 { int f0() { return 45; } };
struct D : M1, M2, M3 {
    int f0() { return 100; }
    int f2() { return 102; }
};

int main()
{
    D d;
    M1 &m1 = d;
    M2 &m2 = d;
    M3 &m3 = d;
    B3 &b3 = m3;
    B1 &b1 = m2;
    B2 &b2 = m1;
    printf("%d %d %d %d\n", m1.f0(), m1.f1(), m1.f2(), m1.f3());
    printf("%d %d %d %d\n", m2.f0(), m2.f1(), m2.f2(), m2.f3());
    printf("%d %d %d %d\n", m3.f0(), m3.f1(), m3.f2(), m3.f3());
    printf("%d %d %d\n", b3.f3(), b1.f1(), b2.f2());
    E5 e;
    B0 &e0 = e;
    printf("%d\n", e0.f0());
    return 0;
}
