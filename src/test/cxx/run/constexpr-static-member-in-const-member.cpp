/* A static constexpr member named in a default template argument that a
   member function's call substitutes: no this is read (Highway's
   Status::CodeName and IsStatusEnum) */
template <bool B, class T = void> struct En {};
template <class T> struct En<true, T> {
    using type = T;
};
using C = unsigned short;
struct S {
    template <class T> static constexpr bool Is() { return sizeof(T) == 2; }
    static int N(C c) { return c; }
    template <class T, typename En<Is<T>()>::type * = nullptr>
    static int N(T v)
    {
        return N(static_cast<C>(v)) + 1;
    }
    int M(C c) const { return N(c); }
};

int main() { return S().M(3) != 3; }
