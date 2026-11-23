// overloading: [over.match], [over.call.func], [over.ics.rank], [over.match.best]
#include <stdio.h>

int f(int) { return 1; }
int f(double) { return 2; }
int f(char) { return 3; }
int f(const char *) { return 4; }
int f(void *) { return 5; }

// [over.ics.rank]/3.2.2: Promotion beats Conversion
int g(int) { return 10; }
int g(long) { return 11; }

// [over.ics.rank]/3.2.6: less cv-qualified reference wins
int h(int &) { return 20; }
int h(const int &) { return 21; }

// [over.ics.rank]/3.2.3: rvalue reference to an rvalue
int k(const int &) { return 30; }
int k(int &&) { return 31; }

// [over.ics.rank]/4.1: pointer to bool is worse
int b(bool) { return 40; }
int b(void *) { return 41; }

// [over.ics.rank]/3.2.5: qualification
int q(const int *) { return 50; }
int q(const volatile int *) { return 51; }

struct A {
    int v;
};
struct B : A {
    int w;
};
struct C : B {
    int x;
};

// [over.ics.rank]/4.4: conversion to the nearer base
int d(A *) { return 60; }
int d(B *) { return 61; }
int r(A &) { return 62; }
int r(B &) { return 63; }
int vp(void *) { return 64; }
int vp(A *) { return 65; }

struct S {
    int n;
    int get() { return n; }
    int get() const { return n + 100; }
    int add(int a) { return n + a; }
    int add(double) { return n + 1000; }
    int use() { return add(1) + add(2.0); }
};

namespace N {
struct T {
    int n;
};
int adl(T t) { return t.n; }
int adl(T t, int) { return t.n + 1; }
} // namespace N

int adl(int) { return 70; }

// [over.match.viable]/2: an ellipsis
int e(int, ...) { return 80; }
int e(int, int) { return 81; }

enum Color { red, green };
int en(int) { return 90; }
int en(long) { return 91; }
int en(unsigned) { return 92; }

// [over.match.ctor] and [over.ics.user]
struct P {
    int v;
    P(int a) : v(a) {}
    P(double) : v(2) {}
    P(int a, int b) : v(a + b) {}
};
int u(P p) { return p.v + 100; }
int u(long) { return 200; }
int w(P p) { return p.v; }
int w(const char *) { return 300; }

int main() {
    int total = 0;
    char c = 'x';
    short s = 3;
    float fl = 1.0f;
    total += f(1) + f(2.0) + f(c) + f("s") + f((void *)0);
    total += f(s);          // short -> int promotion
    total += f(fl) * 100;   // float -> double promotion
    total += g(s) + g(1L);
    int i = 0;
    const int ci = 0;
    total += h(i) + h(ci) + h(5);
    total += k(i) + k(5);
    int *ip = &i;
    total += b(ip) + b(true);
    total += q(ip);
    C cc;
    total += d(&cc) + r(cc) + vp(&cc);
    S so{5};
    const S cs{6};
    total += so.get() + cs.get() + so.use();
    N::T t{7};
    total += adl(t) + adl(t, 1) + adl(1);
    total += e(1, 2) + e(1, 2.0) + e(1, 2, 3);
    total += en(red);
    P p1(1), p2(1.5), p3(1, 2);
    P p4 = 5;
    P p5(p3);
    total += p1.v + p2.v + p3.v + p4.v + p5.v;
    total += u(1) + w(9) + w("w");
    printf("%d\n", total);
    return total & 255;
}
