/* a constexpr vector initialized by a call that returns a scalar in every
   lane: constant initialization, so a dynamic initializer declared before
   it reads its value */
extern "C" int printf(const char *, ...);

typedef float F __attribute__((ext_vector_type(4)));
typedef int I32 __attribute__((ext_vector_type(4)));

static constexpr F F_(float x) { return x; }
static constexpr I32 I32_(int x) { return x; }

extern const F F1;
static float before() { return F1[3]; }
float early = before();

extern constexpr F F0 = F_(0.0f), F2 = F_(2.5f);
extern constexpr F F1 = F_(1.0f);
extern constexpr I32 minus = I32_(-7);

int main()
{
    static constexpr F local = F_(4.0f);
    constexpr F automatic = F_(8.0f);
    for (int i = 0; i < 4; i++)
        printf("%g %g %g %d %g %g\n", F0[i], F1[i], F2[i], minus[i],
               local[i], automatic[i]);
    printf("%g\n", early);
    return 0;
}
