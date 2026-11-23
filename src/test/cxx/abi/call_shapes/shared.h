/* [abi:calls] across compilers, one shape per function: empty classes as
   arguments and results (C++ ignores them on AArch64, C does not),
   homogeneous floating-point aggregates whose members are in bases,
   classes non-trivial for the purposes of calls (by address, the result
   through x8: [class.temporary]/3), dynamic classes by value, reference
   members, unions and bit-fields in registers, big classes and classes
   past the eighth register on the stack.  Not here: classes through
   `...` (no va_arg in the C++ front end yet). */
#ifndef SHARED_H
#define SHARED_H

struct Empty {};
struct Empty2 : Empty {};

struct XY {
    float x, y;
};
/* a homogeneous aggregate of 4 floats, two of them in a base */
struct XYZW : XY {
    float z, w;
};
struct D2 {
    double a;
};
struct D3 : D2 {
    double b, c;
};
/* not homogeneous: a float and a double */
struct Mixed : XY {
    double d;
};

/* non-trivial only through the copy constructor, only through the
   destructor */
struct Copied {
    Copied(int v);
    Copied(const Copied &o);
    int v;
};
struct Destroyed {
    int v;
    ~Destroyed();
    static int destroyed;
};

/* a dynamic class: non-trivial for the purposes of calls */
struct Dyn {
    Dyn(int v);
    virtual int get() const;
    int v;
};

struct Ref {
    int &r;
    const long &c;
};

union U {
    float f;
    int i;
    char c[6];
};
struct BitsU {
    unsigned a : 5;
    unsigned b : 11;
    U u;
};

/* bigger than 16 bytes: by address, the copy the caller's */
struct Big {
    long a, b, c;
    char d;
};

struct Small {
    char c;
    short s;
};

int empty_mid(int a, Empty e, int b, Empty2 f, int c);
Empty empty_ret(int a);
Empty2 empty2_ret(Empty e);
float hfa_sum(XYZW v, float scale);
XYZW hfa_make(float a, float b, float c, float d);
double hfa_d3(D3 d, int n);
D3 hfa_d3_make(double a);
double mixed_sum(Mixed m);
Mixed mixed_make(float x, double d);
int copied_take(Copied a, int b, Copied c);
Copied copied_make(int v);
int destroyed_take(Destroyed d);
Destroyed destroyed_make(int v);
int dyn_take(Dyn d);
Dyn dyn_make(int v);
int ref_sum(Ref r);
Ref ref_make(int &r, const long &c);
int bitsu_sum(BitsU b);
BitsU bitsu_make(unsigned a, unsigned b, int i);
long big_sum(Big b);
Big big_make(long a);
long many(Small a, Small b, Small c, Small d, Small e, Small f, Small g,
          Small h, Small i, Big j, XYZW k, D3 l, Copied m, Small n);

#endif
