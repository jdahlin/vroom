/* [ext:gnu_vector_size] vectors of different element types, one of them
   floating, do not convert to each other: overload resolution picks the
   exact one (x86's __m128, __m128d, __m128i; Highway's BitCastToInteger) */
typedef float F4 __attribute__((__vector_size__(16)));
typedef double D2 __attribute__((__vector_size__(16)));
typedef long long L2 __attribute__((__vector_size__(16)));

static int which(F4) { return 1; }
static int which(D2) { return 2; }
static int which(L2) { return 3; }

int main()
{
    F4 f = {1, 2, 3, 4};
    D2 d = {1, 2};
    L2 l = {1, 2};
    return which(f) != 1 || which(d) != 2 || which(l) != 3;
}
