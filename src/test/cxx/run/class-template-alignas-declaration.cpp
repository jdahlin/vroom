// std: c++20
// [dcl.align]/1: alignas in the declaration of a class template that is
// defined later, with the same alignas (Skia's skvx::Vec)
template <int N, typename T>
struct alignas(N * sizeof(T)) Vec;
template <int N, typename T>
struct alignas(N * sizeof(T)) Vec {
    T val[N];
};
int main()
{
    Vec<4, float> v;
    v.val[0] = 1;
    static_assert(alignof(Vec<4, float>) == 16, "");
    static_assert(alignof(Vec<2, char>) == 2, "");
    if (alignof(Vec<4, float>) != 16)
        return 1;
    return 0;
}
