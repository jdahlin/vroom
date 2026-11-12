/* What a struct field update compiles to through VIR at -O2: the field's address
   is each load's and store's own operand (also when one field is read and
   written in a block), a narrow store takes its value without the extension the
   cast made, a subtraction supplies the flags of the compare of the same
   operands, and a select's zero arm is the zero register.  Offsets beyond the
   addressing modes, unaligned ones (packed), negative ones and narrow stores of
   wider values are the corners. */
#include <stdio.h>
#include <string.h>
#define NI __attribute__((noinline))

struct S {
    unsigned char c; signed char sc; unsigned short h; short sh;
    int i; unsigned u; long l; unsigned short tab[8];
    char pad[40000]; int far; long farl;
};
struct __attribute__((packed)) P { char c; long l; int i; short h; };

/* read and written in one block, several fields */
NI int update(struct S *s, int x)
{
    s->i += x;
    s->u -= (unsigned)x;
    s->l += s->i;
    s->h++;
    s->i ^= s->h;
    return s->i + (int)s->u;
}

/* narrow stores of wider and of extended values */
NI void narrow(struct S *s, long v, int w)
{
    s->c = (unsigned char)v;
    s->sc = (signed char)(w + 1);
    s->h = (unsigned short)(v >> 4);
    s->sh = (short)(unsigned short)w;
    s->i = (int)v;
    s->u = (unsigned)(unsigned char)w;
    s->l = (short)v;
}

/* the subtraction and the compare of the same operands, every kind of condition */
NI unsigned satsub(unsigned m, unsigned w) { return m >= w ? m - w : 0; }
NI int ssub(int m, int w) { return m < w ? 0 : m - w; }
NI long lsub(long m, long w) { long d = m - w; return m > w ? d : -d; }
NI unsigned ksub(unsigned m) { return m >= 100 ? m - 100 : 0; }
NI unsigned self(unsigned m, unsigned w) { m = m - w; return m >= w ? m : 0; }   /* a different compare */

/* the table slide: load, saturating subtract, narrow store */
NI void slide(unsigned short *p, unsigned n, unsigned wsize)
{
    do {
        unsigned m = *--p;
        *p = (unsigned short)(m >= wsize ? m - wsize : 0);
    } while (--n);
}

/* offsets past the scaled range, and at its edges */
NI long far(struct S *s, int x)
{
    s->far += x;
    s->farl -= s->far;
    s->pad[39999] = (char)x;
    s->pad[255] ^= 1; s->pad[256] ^= 2; s->pad[4095] ^= 4; s->pad[4096] ^= 8;
    return s->farl + s->far + s->pad[39999];
}

/* unaligned fields */
NI long packed(struct P *p, long x)
{
    p->l += x;
    p->i -= (int)x;
    p->h = (short)(p->h + p->i);
    p->l ^= p->h;
    return p->l + p->i;
}

/* negative offsets from a pointer into the middle */
NI int back(int *p, int x)
{
    p[-1] += x;
    p[-2] -= p[-1];
    p[-64] ^= p[-2];
    p[-65] += p[-64];
    return p[-1] + p[-2] + p[-64] + p[-65];
}

/* select with a zero arm, both ways round, 32 and 64 bit */
NI int z1(int c, int v) { return c > 3 ? v : 0; }
NI long z2(long c, long v) { return c ? 0 : v; }

int main(void)
{
    static struct S s;
    struct P p = { 1, 0x1122334455667788L, -7, 300 };
    unsigned short t[6] = { 0, 1, 99, 100, 101, 65535 };
    int a[70];

    s.i = 5, s.u = 7, s.l = 9, s.h = 65535;
    printf("update %d %d %u %ld %u\n", update(&s, 3), s.i, s.u, s.l, s.h);
    narrow(&s, 0x123456789abcdef0L, 0x1ff7f);
    printf("narrow %u %d %u %d %d %u %ld\n", s.c, s.sc, s.h, s.sh, s.i, s.u, s.l);
    narrow(&s, -2, -1);
    printf("narrow %u %d %u %d %d %u %ld\n", s.c, s.sc, s.h, s.sh, s.i, s.u, s.l);
    printf("sub %u %u %u %d %d %d %ld %ld %u %u %u %u\n", satsub(5, 3), satsub(3, 5), satsub(0, 0xffffffffu),
           ssub(-5, 3), ssub(3, -5), ssub(-2147483647 - 1, 1), lsub(3, 9), lsub(9, 3),
           ksub(99), ksub(100), self(10, 3), self(5, 3));
    slide(t + 6, 6, 100);
    printf("slide %u %u %u %u %u %u\n", t[0], t[1], t[2], t[3], t[4], t[5]);
    s.far = 40, s.farl = 1000;
    printf("far %ld %d %ld %d %d %d %d\n", far(&s, 2), s.far, s.farl, s.pad[255], s.pad[256], s.pad[4095], s.pad[4096]);
    printf("packed %ld %ld %d %d\n", packed(&p, 5), p.l, p.i, p.h);
    for (int i = 0; i < 70; i++)
        a[i] = i * 3 + 1;
    printf("back %d %d %d %d %d\n", back(a + 66, 4), a[65], a[64], a[2], a[1]);
    printf("zero %d %d %ld %ld\n", z1(4, 9), z1(3, 9), z2(0, 8), z2(2, 8));
    return 0;
}
