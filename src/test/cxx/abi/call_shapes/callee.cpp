#include "shared.h"

int Destroyed::destroyed;

Copied::Copied(int v) : v(v)
{
}

Copied::Copied(const Copied &o) : v(o.v + 100)
{
}

Destroyed::~Destroyed()
{
    destroyed += v;
}

Dyn::Dyn(int v) : v(v)
{
}

int Dyn::get() const
{
    return v * 3;
}

int empty_mid(int a, Empty e, int b, Empty2 f, int c)
{
    return a * 100 + b * 10 + c;
}

Empty empty_ret(int a)
{
    Empty e;
    return e;
}

Empty2 empty2_ret(Empty e)
{
    Empty2 f;
    return f;
}

float hfa_sum(XYZW v, float scale)
{
    return (v.x + v.y * 2 + v.z * 3 + v.w * 4) * scale;
}

XYZW hfa_make(float a, float b, float c, float d)
{
    XYZW v;
    v.x = a;
    v.y = b;
    v.z = c;
    v.w = d;
    return v;
}

double hfa_d3(D3 d, int n)
{
    return d.a + d.b * 2 + d.c * 3 + n;
}

D3 hfa_d3_make(double a)
{
    D3 d;
    d.a = a;
    d.b = a + 1;
    d.c = a + 2;
    return d;
}

double mixed_sum(Mixed m)
{
    return m.x + m.y + m.d;
}

Mixed mixed_make(float x, double d)
{
    Mixed m;
    m.x = x;
    m.y = x * 2;
    m.d = d;
    return m;
}

int copied_take(Copied a, int b, Copied c)
{
    return a.v * 1000 + b * 10 + c.v;
}

Copied copied_make(int v)
{
    return Copied(v);
}

int destroyed_take(Destroyed d)
{
    return d.v;
}

Destroyed destroyed_make(int v)
{
    Destroyed d;
    d.v = v;
    return d;
}

int dyn_take(Dyn d)
{
    return d.get() + d.v;
}

Dyn dyn_make(int v)
{
    return Dyn(v);
}

int ref_sum(Ref r)
{
    r.r += 1;
    return r.r + static_cast<int>(r.c);
}

Ref ref_make(int &r, const long &c)
{
    Ref x = {r, c};
    return x;
}

int bitsu_sum(BitsU b)
{
    return static_cast<int>(b.a + b.b) + b.u.i;
}

BitsU bitsu_make(unsigned a, unsigned b, int i)
{
    BitsU x;
    x.a = a;
    x.b = b;
    x.u.i = i;
    return x;
}

long big_sum(Big b)
{
    long s = b.a + b.b * 2 + b.c * 3 + b.d;
    b.a = 0;
    return s;
}

Big big_make(long a)
{
    Big b;
    b.a = a;
    b.b = a + 1;
    b.c = a + 2;
    b.d = 7;
    return b;
}

long many(Small a, Small b, Small c, Small d, Small e, Small f, Small g,
          Small h, Small i, Big j, XYZW k, D3 l, Copied m, Small n)
{
    return a.c + b.s + c.c + d.s + e.c + f.s + g.c + h.s + i.c * 1000 +
           j.c * 10 + static_cast<long>(k.w) + static_cast<long>(l.c) + m.v +
           n.s * 7;
}
