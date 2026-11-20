/* [ext:gnu_vector_size] vector_size(N) on a typedef-name and a variable
   (GCC manual 6.37): N bytes, aligned to N up to 16 on arm64 (x86-64: all
   of N), laid out in classes, mangled Dv N _ element; the values are not
   analysed yet (gnu-vector-size-values), only the types. */
typedef int v4si __attribute__((vector_size(16)));
typedef float v8sf __attribute__((__vector_size__(32)));
typedef long long m64 __attribute__((vector_size(8)));
typedef unsigned char v16qu __attribute__((vector_size(16)));
typedef double v1df __attribute__((vector_size(8)));

static_assert(sizeof(v4si) == 16 && alignof(v4si) == 16, "");
static_assert(sizeof(v8sf) == 32, "");
#ifdef __aarch64__
static_assert(alignof(v8sf) == 16, "");
#else
static_assert(alignof(v8sf) == 32, "");
#endif
static_assert(sizeof(m64) == 8 && alignof(m64) == 8, "");
static_assert(sizeof(v16qu) == 16 && sizeof(v1df) == 8, "");

struct holder {
    char tag;
    v4si v;
    m64 m;
};
static_assert(sizeof(holder) == 48 && alignof(holder) == 16, "");

int count(v4si *) { return 1; }
int count(v8sf *) { return 10; }
int count(m64 *) { return 100; }
int count(v16qu *) { return 1000; }

int main()
{
    v4si *a = nullptr;
    v8sf *b = nullptr;
    m64 *c = nullptr;
    v16qu *d = nullptr;
    return count(a) + count(b) + count(c) + count(d) == 1111 ? 0 : 1;
}
