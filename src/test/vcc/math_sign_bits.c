extern double fabs(double), copysign(double, double);
extern float fabsf(float), copysignf(float, float);
static volatile int calls;
static double once(double x) { calls++; return x; }
static const double folded = __builtin_copysign(-3.0, 2.0);
int main(void)
{
  const unsigned long long doubles[] = {0,0x8000000000000000ULL,0xfff0000000000000ULL,
    0xfff8000000001234ULL,0xfff0000000001234ULL,0x8000000000000001ULL};
  const unsigned floats[] = {0,0x80000000U,0xff800000U,0xffc01234U,0xff801234U,0x80000001U};
  for (unsigned i=0; i<6; i++) {
    union { unsigned long long u; double f; } x={.u=doubles[i]}, a,b,c,d;
    union { unsigned u; float f; } y={.u=floats[i]}, e,f,g,h;
    a.f=fabs(x.f); b.f=__builtin_fabs(x.f);
    c.f=copysign(x.f, -0.0); d.f=__builtin_copysign(x.f, 0.0);
    e.f=fabsf(y.f); f.f=__builtin_fabsf(y.f);
    g.f=copysignf(y.f, -0.0f); h.f=__builtin_copysignf(y.f, 0.0f);
    if (a.u!=(x.u&0x7fffffffffffffffULL) || b.u!=a.u || c.u!=(x.u|0x8000000000000000ULL) || d.u!=a.u) return 1;
    if (e.u!=(y.u&0x7fffffffU) || f.u!=e.u || g.u!=(y.u|0x80000000U) || h.u!=e.u) return 2;
    /* A signaling NaN sign source must not quiet the first operand. */
    c.f=copysign(1.0, x.f);
    g.f=copysignf(1.0f,y.f);
    if(c.u!=(0x3ff0000000000000ULL|(x.u&0x8000000000000000ULL))) return 3;
    if(g.u!=(0x3f800000U|(y.u&0x80000000U))) return 4;
  }
  if (copysign(once(3.), once(-2.)) != -3. || calls != 2 || folded != 3.) return 5;
  return 0;
}
