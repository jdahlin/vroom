/* A floating-point pragma anywhere in the unit must not make a constant
   that fits a narrowing conversion ([dcl.init.list]/7), as in clang's
   xmmintrin.h beside a "#pragma clang fp". */
typedef float v4 __attribute__((__vector_size__(16)));

static v4 load_low(float x) { return (v4){x, 0, 0, 0}; }

#pragma clang fp reassociate(off)

int main()
{
    v4 v = load_low(2.5f);
    float f{3};
    float g{0.1};
    double d[2] = {1, 'a'};
    if (v[0] != 2.5f || v[1] != 0 || v[3] != 0)
        return 1;
    if (f != 3 || g != 0.1f || d[0] != 1 || d[1] != 97)
        return 2;
    return 0;
}
