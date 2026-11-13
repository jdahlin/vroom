/* A load that repeats one of a dominating block (VIR at -O2: the second is the
   first's value when nothing between them may write those bytes, also round the
   loops the second one is in).  It must be read again when a store to the same
   bytes, an overlapping narrower or wider store, a store through another
   pointer, or a call lies on some way from the first to the second: after it
   in the loop's body, in an inner loop, in a loop made by goto, or on one arm
   of a branch. */
#include <stdio.h>
#define NI __attribute__((noinline))

struct S { long a; int b; int c; unsigned char *w; int d[4]; };

NI void bump(struct S *s) { s->a += 100; }

/* nothing writes s->w or s->a in the loop: one load each (the store is to s->c) */
NI long clean(struct S *s, int n)
{
    long r = s->a + s->w[0];

    for (int i = 0; i < n; i++) {
        r += s->w[i & 3] + s->a;
        s->c = i;
    }
    return r + s->a;
}

/* the store comes after the load in the body: the next iteration reads it */
NI long late(struct S *s, int n)
{
    long r = s->a;

    for (int i = 0; i < n; i++) {
        r += s->a;
        s->a = r & 7;
    }
    return r;
}

/* through another pointer, which may be the same object */
NI long alias(struct S *s, struct S *t, int n)
{
    long r = s->a;

    for (int i = 0; i < n; i++) {
        r += s->a;
        t->a = i;
    }
    return r + s->a;
}

/* a narrower store into the loaded bytes, and a wider one over them */
NI long overlap(struct S *s, int n)
{
    long r = s->a;
    int x = s->c;

    for (int i = 0; i < n; i++) {
        r += s->a + s->c;
        ((unsigned char *)s)[3] = (unsigned char)(i + 1);     /* inside a */
        *(long *)((char *)s + 8) = 0x100000002L * (i + 1);    /* b and c */
    }
    return r + x + s->c;
}

/* a call in the loop */
NI long call(struct S *s, int n)
{
    long r = s->a;

    for (int i = 0; i < n; i++) {
        r += s->a;
        if (i & 1)
            bump(s);
    }
    return r + s->a;
}

/* the write is in an inner loop after the load */
NI long inner(struct S *s, int n)
{
    long r = s->a;

    for (int i = 0; i < n; i++) {
        r += s->a;
        for (int j = 0; j < i; j++)
            if (j == 2)
                s->a = r & 15;
    }
    return r;
}

/* on one arm between the two loads, no loop */
NI long arm(struct S *s, int k)
{
    long r = s->a;

    if (k & 1)
        s->a = k;
    else
        s->c = k;
    return r + s->a;
}

/* a loop made by goto, entered in the middle; the store is before the head in the text */
NI long gotos(struct S *s, int n)
{
    long r = s->a;
    int i = 0;

    if (n & 1)
        goto mid;
top:
    s->a = r & 3;
mid:
    r += s->a + 1;
    if (++i < n)
        goto top;
    return r;
}

/* an array element: the constant index is clean, the variable one may be it */
NI long elems(struct S *s, int n, int k)
{
    long r = s->d[1];

    for (int i = 0; i < n; i++) {
        r += s->d[1];
        s->d[2] = i;
    }
    r += s->d[1] * 3;
    for (int i = 0; i < n; i++) {
        r += s->d[1];
        s->d[k] = i + 5;
    }
    return r + s->d[1];
}

/* a local whose address escapes, written through the escaped pointer */
NI long local(int n)
{
    long v[2] = { 5, 6 };
    long *volatile vp = v;
    long *p = vp, r = v[0];

    for (int i = 0; i < n; i++) {
        r += v[0] + v[1];
        p[i & 1] = r & 31;
    }
    return r + v[0];
}

int main(void)
{
    unsigned char w[4] = { 1, 2, 3, 4 };
    struct S s = { 7, 8, 9, w, { 1, 2, 3, 4 } }, t = { 70, 80, 90, w, { 5, 6, 7, 8 } };

    printf("clean %ld\n", clean(&s, 5));
    printf("late %ld\n", late(&s, 6));
    s.a = 7;
    printf("alias %ld %ld\n", alias(&s, &t, 4), alias(&s, &s, 4));
    s.a = 7;
    printf("overlap %ld\n", overlap(&s, 3));
    s.a = 7;
    printf("call %ld\n", call(&s, 5));
    s.a = 7;
    printf("inner %ld\n", inner(&s, 6));
    s.a = 7;
    printf("arm %ld %ld\n", arm(&s, 3), arm(&s, 4));
    s.a = 7;
    printf("gotos %ld", gotos(&s, 4));
    s.a = 7;
    printf(" %ld\n", gotos(&s, 5));
    printf("elems %ld %ld\n", elems(&s, 3, 0), elems(&s, 3, 1));
    printf("local %ld\n", local(5));
    return 0;
}
