/* [class.mi] across compilers: virtual bases laid out by one half and
   reached by the other through the vbase offsets of its virtual tables;
   the caller's class D constructs and destroys the callee's B and C with
   their base object variants and its own VTT's sub-VTTs
   ([abi:vtable-ctor-vtt]), whose construction virtual tables are the
   caller's. */
#ifndef SHARED_H
#define SHARED_H

extern int trace[16];
extern int traced;

/* their virtual tables, VTTs and constructors are the callee's */
struct A {
    A(int a);
    ~A();
    virtual int value() const;
    int a;
};

struct B : virtual A {
    B(int b);
    ~B();
    virtual int from_b() const;
    int b;
};

struct C : virtual A {
    C(int c);
    ~C();
    virtual int from_c() const;
    int c;
};

/* its virtual table, VTT and construction virtual tables: the caller's */
struct D : B, C {
    D(int d);
    ~D();
    int d;
};

int value_of(const A *a);
int b_of(const B &b);
int c_of(const C *c);
int a_through_c(const C *c);
const B *callee_b();

#endif
