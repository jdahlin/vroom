// X::Y::f defined outside X: X's names are in scope after Y's (Skia's
// SkYUVAPixmapInfo::SupportedDataTypes::All using SkYUVAPixmapInfo::DataType)
struct X {
    enum class D { a };
    using T = char;
    struct Y {
        using T = long;
        static int f();
    };
};
int X::Y::f()
{
    static_assert(sizeof(T) == sizeof(long));
    int n = 0;
    return static_cast<D>(n) == D::a;
}
