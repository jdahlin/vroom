/* [class.base.init]/9.1, [dcl.init]/8.2: a default member initializer {}
   of a class with a trivial default constructor zero-initializes the
   member in place, though the class cannot be copied (libc++'s
   std::atomic<int> in doctest) */
extern "C" void *memset(void *, int, __SIZE_TYPE__);
struct Counter {
    int v;
    long w;
    Counter() = default;
    constexpr Counter(int x) : v(x), w(x) {}
    Counter(const Counter &) = delete;
};
struct Lanes {
    Counter first{};
    Counter second = {};
    char pad[4];
};
alignas(Lanes) static unsigned char storage[sizeof(Lanes)];
inline void *operator new(__SIZE_TYPE__, void *p, int) noexcept
{
    return p;
}
int main()
{
    memset(storage, 0x55, sizeof storage);
    Lanes *l = new (storage, 0) Lanes;
    return l->first.v || l->first.w || l->second.v || l->second.w;
}
