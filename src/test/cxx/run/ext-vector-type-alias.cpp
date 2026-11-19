/* [ext:clang_ext_vector_type] ext_vector_type(N) on an alias template, as
   libc++'s __algorithm/simd_utils.h declares __simd_vector: clang's layout
   (the elements' bytes rounded up to a power of two, aligned to it up to
   16) and mangling (Dv N _ element). */
template <class T, unsigned long N>
using vec __attribute__((__ext_vector_type__(N))) = T;

using float3 __attribute__((ext_vector_type(3))) = float;

static_assert(sizeof(vec<char, 4>) == 4 && alignof(vec<char, 4>) == 4, "");
static_assert(sizeof(float3) == 16 && alignof(float3) == 16, "");
static_assert(sizeof(vec<double, 8>) == 64 && alignof(vec<double, 8>) == 16,
              "");
static_assert(sizeof(vec<short, 3>) == 8, "");

struct holder {
    char tag;
    vec<int, 2> v;
};
static_assert(sizeof(holder) == 16 && alignof(holder) == 8, "");

int count(vec<float, 4> *) { return 4; }
int count(vec<int, 4> *) { return 40; }
int count(float3 *) { return 3; }

int main()
{
    vec<float, 4> *f = nullptr;
    vec<int, 4> *i = nullptr;
    float3 *t = nullptr;
    return count(f) + count(i) + count(t) == 47 ? 0 : 1;
}
