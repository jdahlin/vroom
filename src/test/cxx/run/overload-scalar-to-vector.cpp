/* [ext:clang_ext_vector_type] a scalar argument for a vector parameter in
   overload resolution: viable, and worse than a conversion to a scalar */
extern "C" int printf(const char *, ...);

typedef float F __attribute__((ext_vector_type(4)));

static F spread(float x) { return x; }
static F nmad(F f, F m, F a) { return a - f * m; }
static F nmad(F f, float m, F a) { return nmad(f, spread(m), a); }
static F nmad(F f, float m, float a) { return nmad(f, spread(m), spread(a)); }

static int pick(F) { return 1; }
static int pick(int) { return 2; }
static int flag(F) { return 3; }
static int flag(bool) { return 4; }
static float lane(F v) { return v[3]; }
static int two(F, int) { return 5; }
static int two(F, long) { return 6; }

int main(int argc, char **)
{
    F m = spread(float(argc)), e = spread(10.0f);
    F r = nmad(m, 1.5f, e - 4.0f) - 3.0f / (0.5f + m);
    printf("%g %g\n", r[0], r[3]);
    printf("%d %d %g %d\n", pick(1.5f), flag(argc), lane(argc), two(2.5f, 1));
    return 0;
}
