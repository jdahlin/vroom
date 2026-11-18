/* Classes both halves lay out, pass and return (doc/PLAN-cxx-sema.md C5.3):
   the Itanium ABI's layout ([abi:class-types]), its symbols, and the
   classes that are non-trivial for the purposes of calls ([abi:calls]). */
#ifndef SHARED_H
#define SHARED_H

struct Empty {};

struct Pod {
    char c;
    int i;
};

/* not a POD for the purpose of layout, without tail padding (with it:
   ../tail_padding) */
struct NonPod {
    NonPod(int i, char c);
    int i;
    int c;
};

struct Derived : NonPod {
    Derived(int i, char c, char d);
    char d;
};

struct WithEmpty : Empty {
    int x;
};

struct Bits {
    unsigned a : 3;
    int b : 7;
    unsigned : 0;
    unsigned c : 9;
    char d;
};

union Either {
    int i;
    char c[7];
};

/* non-trivial for the purposes of calls: passed by address, returned
   through the caller's storage */
struct Counted {
    Counted(int id);
    Counted(const Counted &other);
    ~Counted();
    int id;
    static int live;
    static int copies;
};

struct Member {
    int v;
    int get() const;
    void set(int to);
    static int twice(int n);
};

namespace space
{
int scaled(int n);
extern int scale;
} // namespace space

/* defined by the callee */
Pod pod_swap(Pod p);
int derived_sum(const Derived &d);
Derived derived_make(int i, char c, char d);
int with_empty(WithEmpty w, Empty e, int after);
int bits_sum(Bits b);
Bits bits_make(unsigned a, int b, unsigned c, char d);
Either either_of(int i);
int counted_take(Counted c);
Counted counted_make(int id);
int call_back(int id);
extern int initialized_first;

/* defined by the caller, called by the callee */
int called_back(Counted c, Derived d, Bits b);

#endif
