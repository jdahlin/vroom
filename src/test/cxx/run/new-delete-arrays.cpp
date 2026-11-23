// arrays of classes allocated by new[] and freed by delete[]: each element
// constructed in order ([expr.new]/22) and destroyed in reverse order
// ([expr.delete]/6); [abi:array-cookies]: the count stored before the
// elements of a class with a non-trivial destructor, the deallocation
// function given the cookie's address and the whole size ([expr.delete]/11)
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long size_t;

static size_t last_new, last_delete;
void *operator new(size_t n) { return malloc(n); }
void *operator new[](size_t n)
{
    last_new = n;
    return malloc(n);
}
void operator delete(void *p) { free(p); }
void operator delete(void *p, size_t) { free(p); }
void operator delete[](void *p) { free(p); }
void operator delete[](void *p, size_t n)
{
    last_delete = n;
    free(p);
}

static int order[32];
static int noted;
static int next_id;

struct Tracked {
    int id;
    Tracked() : id(next_id++) {}
    ~Tracked() { order[noted++] = id; }
};

// a constructor and no destructor: no cookie
struct Built {
    int v;
    Built() : v(7) {}
};

// a destructor and no constructor: a cookie, nothing to construct
static int gone;
struct Only {
    long a, b;
    ~Only() { gone++; }
};

// aligned to 16 (long double on AArch64): 16 bytes of cookie
struct Wide {
    long double d;
    char c;
    Wide() : d(0), c(1) {}
    ~Wide() { gone += c; }
};

// class-specific array allocation, unsized deallocation
static int pool_arrays;
struct Pooled {
    int x;
    Pooled() : x(3) {}
    ~Pooled() { x = 0; }
    void *operator new[](size_t n)
    {
        pool_arrays++;
        return malloc(n);
    }
    void operator delete[](void *p)
    {
        pool_arrays--;
        free(p);
    }
};

static size_t count(size_t n) { return n; }
static size_t cookie(void *p) { return ((size_t *)p)[-1]; }

int main()
{
    int failures = 0;
    size_t n = count(4);
    Tracked *t = new Tracked[n];
    failures += last_new != 4 * sizeof(Tracked) + sizeof(size_t);
    failures += cookie(t) != 4;
    for (int i = 0; i < 4; i++)
        failures += t[i].id != i;
    delete[] t;
    failures += last_delete != 4 * sizeof(Tracked) + sizeof(size_t);
    failures += noted != 4;
    for (int i = 0; i < 4; i++)
        failures += order[i] != 3 - i;

    Tracked *k = new Tracked[3]; // a constant count
    failures += last_new != 3 * sizeof(Tracked) + sizeof(size_t);
    failures += k[2].id != 6;
    delete[] k;
    failures += noted != 7 || order[4] != 6 || order[6] != 4;

    Built *b = new Built[count(5)];
    failures += last_new != 5 * sizeof(Built);
    failures += b[4].v != 7;
    delete[] b;

    Only *o = new Only[count(2)];
    failures += last_new != 2 * sizeof(Only) + sizeof(size_t);
    delete[] o;
    failures += gone != 2;

    Wide *w = new Wide[count(3)];
    failures += last_new != 3 * sizeof(Wide) + 16;
    delete[] w;
    failures += gone != 5;

    Pooled *p = new Pooled[count(2)];
    failures += pool_arrays != 1 || p[1].x != 3;
    delete[] p;
    failures += pool_arrays != 0;

    Tracked *none = 0;
    delete[] none; // nothing for a null pointer ([expr.delete]/6)
    Tracked *empty = new Tracked[count(0)];
    delete[] empty;
    failures += noted != 7;

    // value-initialization with a user-provided default constructor:
    // default-initialization ([dcl.init]/8.1)
    Tracked *one = new Tracked();
    failures += one->id != next_id - 1;
    delete one;
    failures += noted != 8;
    printf("new-delete-arrays: %d failures\n", failures);
    return failures;
}
