/* References to enumerations ([dcl.init.ref]/5): an enumeration has no
   conversion functions, so a reference binds to an lvalue or rvalue of
   the same enumeration as to a fundamental type's.  libc++'s std::byte:
   operator|= returns the assignment's lvalue as byte&.  An assignment or
   prefix increment used as an lvalue ([expr.ass]/1) is lowered with its
   left operand evaluated once. */
extern "C" int printf(const char *, ...);

enum class byte : unsigned char {};
enum color { red, green, blue };

inline constexpr byte operator|(byte a, byte b) noexcept
{
    return static_cast<byte>(static_cast<unsigned char>(
        static_cast<unsigned int>(a) | static_cast<unsigned int>(b)));
}
inline constexpr byte &operator|=(byte &l, byte r) noexcept
{
    return l = l | r;
}

color &pick(color &a, color &b, bool first)
{
    if (first)
        return a;
    return b;
}

int main()
{
    byte b = static_cast<byte>(1);
    byte &same = (b |= static_cast<byte>(4));
    same |= static_cast<byte>(16);
    color c = red, d = blue;
    pick(c, d, false) = green;
    const color &k = c;
    color &&t = static_cast<color &&>(c);
    const volatile color &cv = d;
    printf("%d %d %d %d %d\n", (int)b, (int)d, (int)k, (int)t, (int)cv);
    int a[4] = {0}, i = 0, x = 1;
    (a[i++] = 5) += 1;
    int &y = ++(++x);
    (a[i++] *= 3) = 7;
    printf("%d %d %d %d %d\n", a[0], a[1], i, x, y);
    return (int)b == 21 && d == green && a[0] == 6 && i == 2 && x == 3 ? 0
                                                                     : 1;
}
