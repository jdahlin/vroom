/* [dcl.stc]/8, [expr.ref]/6.2: a mutable member of a const object is
   modifiable (an array, a pointer to const, a member of a member); a const
   object with a mutable member is not in read-only memory (it is written
   here); a const member function writes it */
#include <stdio.h>

struct Inner { mutable int hits; int fixed; };
struct S {
    mutable int a;
    mutable int arr[3];
    mutable const char *p;
    mutable unsigned long long big;
    Inner in;
    int b;
    int bump() const { return ++a; }
    int touch() const { in.hits += 5; return in.hits; }
};

const S global = {1, {2, 3, 4}, "x", 9, {7, 8}, 6};
struct Derived : S { int c; };
const Derived derived = {{10, {0, 0, 0}, 0, 0, {0, 0}, 0}, 3};

int use(const S &s)
{
    s.a += 10;
    s.arr[2] = 40;
    s.p = "y";
    s.in.hits = 3;
    return s.a + s.arr[2] + s.in.hits;
}

int main()
{
    const S local = {1, {2, 3, 4}, "x", 9, {7, 8}, 6};
    int r = use(local);
    r += local.bump();
    r += use(global);
    r += global.touch();
    derived.a = 5;
    r += derived.a + derived.c;
    printf("%d %d %s %d\n", r, global.a, global.p, local.in.hits);
    return 0;
}
