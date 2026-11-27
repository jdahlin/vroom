// [ext:clang_ext_vector_type] no conversion between different extended
// vector types, so only the overload of the argument's type is viable
extern "C" int printf(const char *, ...);

typedef float F __attribute__((ext_vector_type(4)));
typedef int I __attribute__((ext_vector_type(4)));

static I pick(I c, F t, F) { return c & static_cast<I>(1) | (I)t; }
static I pick(I c, I t, I e) { return (t & c) | (e & ~c); }

__attribute__((noinline)) static I smaller(I a, I b)
{
    return pick(a < b, a, b);
}

int main()
{
    I a = {1, 9, -3, 4}, b = {5, 2, 7, 4};
    I m = smaller(a, b);
    printf("%d %d %d %d\n", m[0], m[1], m[2], m[3]);
}
