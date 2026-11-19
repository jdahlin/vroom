/* [ext:gnu_attributes] aligned on a typedef-name: objects and members of
   the type through that name have the alignment, alignof(name) is it, the
   type is the aliased one, a pointer to it and sizeof are not changed */
#include <stddef.h>
template <class T, class U> struct same {
    static constexpr bool value = false;
};
template <class T> struct same<T, T> {
    static constexpr bool value = true;
};

typedef int A __attribute__((aligned(16)));
typedef struct {
    char c;
} S __attribute__((aligned));
typedef struct {
    void *pad[4];
} U __attribute__((__aligned__));
typedef A B;
typedef char C __attribute__((aligned(8)));
typedef A *PA; /* a pointer is not aligned by it */

struct M {
    char c;
    A a;
    char d;
    S s;
    A *p;
    U arr[2];
    C e;
};
struct N {
    char c;
    B b;
};

static_assert(same<A, int>::value && same<S *, decltype(&*(S *)0)>::value);
static_assert(alignof(A) == 16 && sizeof(A) == 4);
static_assert(alignof(S) == 16 && sizeof(S) == 1);
static_assert(alignof(U) == 16 && sizeof(U) == 32);
static_assert(alignof(B) == 16 && alignof(PA) == 8);
static_assert(alignof(U[3]) == 16 && alignof(A *) == 8);
static_assert(alignof(C) == 8 && sizeof(C) == 1);
static_assert(offsetof(M, a) == 16 && offsetof(M, d) == 20);
static_assert(offsetof(M, s) == 32 && offsetof(M, p) == 40);
static_assert(offsetof(M, arr) == 48 && offsetof(M, e) == 112);
static_assert(alignof(M) == 16 && sizeof(M) == 128);
static_assert(alignof(N) == 16 && sizeof(N) == 32);

A g;
U gs[2];
static A h;

int main()
{
    A l;
    S ls;
    A *p = &l;
    M m;
    if (reinterpret_cast<unsigned long>(&g) % 16 ||
        reinterpret_cast<unsigned long>(&gs) % 16 ||
        reinterpret_cast<unsigned long>(&h) % 16 ||
        reinterpret_cast<unsigned long>(&l) % 16 ||
        reinterpret_cast<unsigned long>(&ls) % 16 ||
        reinterpret_cast<unsigned long>(&m) % 16 || !p)
        return 1;
    return 0;
}
