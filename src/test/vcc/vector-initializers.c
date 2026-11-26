/* Vectors made of their elements (the set intrinsics): equal and distinct elements of each
   width, and stores that something else comes between. */
int printf(const char *, ...);

typedef signed char v16 __attribute__((vector_size(16)));
typedef short v8 __attribute__((vector_size(16)));
typedef int v4 __attribute__((vector_size(16)));
typedef long long v2 __attribute__((vector_size(16)));

static void show(const char *name, const void *p)
{
    const unsigned char *c = p;
    printf("%s ", name);
    for (int i = 0; i < 16; i++)
        printf("%02x", c[i]);
    printf("\n");
}

__attribute__((noinline)) static void make(char b, short h, int w, long long q,
                                           int *alias)
{
    v16 b1 = {b, b, b, b, b, b, b, b, b, b, b, b, b, b, b, b};
    v16 bn = {b, 1,  b + 2, 3,  4,  b, 6, -7,
              8, -b, 10,    11, 12, 13, 14, b};
    v16 bw = {b, 1, 2, 3, b, 1, 2, 3, b, 1, 2, 3, b, 1, 2, 3};
    v8 h1 = {h, h, h, h, h, h, h, h};
    v8 hn = {h, -1, h + 1, 3, -h, 5, 6, h};
    v4 w1 = {w, w, w, w};
    v4 wn = {w, 2, w, -4};
    v2 q1 = {q, q};
    v2 qn = {q, -q};
    v4 partial = {w, w};
    union {
        v4 v;
        int i[4];
    } u;
    u.i[0] = w;
    u.i[1] = w;
    u.i[2] = w;
    *alias = 99;
    u.i[3] = w;
    v4 first = u.v;
    u.i[0] = 1;
    u.i[1] = 2;
    u.i[1] = 3;
    v4 again = u.v;
    show("b1", &b1);
    show("bn", &bn);
    show("bw", &bw);
    show("h1", &h1);
    show("hn", &hn);
    show("w1", &w1);
    show("wn", &wn);
    show("q1", &q1);
    show("qn", &qn);
    show("partial", &partial);
    show("first", &first);
    show("again", &again);
}

int main(void)
{
    int alias = 0;
    make(-3, 300, -70000, 0x123456789abcdefLL, &alias);
    make(127, -32768, 0x7fffffff, -1, &alias);
    printf("%d\n", alias);
    return 0;
}
