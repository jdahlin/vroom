// [abi:array-cookies]: an array of a class with a trivial destructor has a
// cookie when its usual operator delete[] takes the size (a class's only
// one), and delete[] passes the allocation's size; with both forms the
// unsized one is selected ([expr.delete]/10) and there is no cookie
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long size_t;

void *operator new[](size_t n)
{
    printf("new[] %lu\n", n);
    return malloc(n);
}
void operator delete[](void *p) { free(p); }
void operator delete[](void *p, size_t) { free(p); }

struct T {
    int v;
    T() : v(3) {}
    static void *operator new[](size_t n)
    {
        printf("T new[] %lu\n", n);
        return malloc(n);
    }
    static void operator delete[](void *p, size_t n)
    {
        printf("T delete[] %lu\n", n);
        free(p);
    }
};

struct U {
    long v;
    static void operator delete[](void *p, size_t n)
    {
        printf("U delete[] %lu\n", n);
        free(p);
    }
};

struct W {
    int v;
    W() : v(1) {}
    static void operator delete[](void *p)
    {
        printf("W delete[]\n");
        free(p);
    }
    static void operator delete[](void *p, size_t n)
    {
        printf("W delete[] %lu\n", n);
        free(p);
    }
};

int main()
{
    T *t = new T[5];
    printf("%d\n", t[4].v);
    delete[] t;
    int k = 4;
    T *t2 = new T[k];
    delete[] t2;
    U *u = new U[3];
    u[2].v = 6;
    delete[] u;
    W *w = new W[2];
    delete[] w;
    return 0;
}
