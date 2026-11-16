// constexpr calls with arguments folded in constant expressions: a function's parameters bound to the
// arguments' values, function template instances (deduced or explicit, a static member template calling
// another), and a compiled-out assertion before the return (Skia's STArray: SkAlignedSTStorage<
// SkContainerAllocator::RoundUp<T>(N), T>, SkAlignTo's SkASSERT under NDEBUG)
typedef unsigned long size_t;
constexpr int f(int x) { return x + 1; }
constexpr unsigned char narrow(unsigned char c) { return c; }
template <typename T> constexpr T SkAlignTo(T x, T a)
{
    static_cast<void>(0);
    return (x + a - 1) & ~(a - 1);
}
struct CA {
    template <typename T> static constexpr size_t RoundUp(size_t n)
    {
        return SkAlignTo(n * sizeof(T), (size_t)kMultiple) / sizeof(T);
    }

  private:
    static constexpr long kMultiple = 8;
};
template <int N> struct A { char b[N]; };
template <int N> struct B : A<f(N)> {};
template <int N, typename T> struct S : A<CA::RoundUp<T>(N)> {};
static_assert(f(2) == 3);
static_assert(narrow(300) == 44);
static_assert(SkAlignTo(5ul, 8ul) == 8);
static_assert(CA::RoundUp<int>(3) == 4);
static_assert(CA::RoundUp<void *>(1) == 1);
static_assert(sizeof(A<f(3)>) == 4);
static_assert(sizeof(B<3>) == 4);
static_assert(sizeof(S<3, int>) == 4);
