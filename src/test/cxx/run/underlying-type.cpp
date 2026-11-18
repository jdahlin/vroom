// [ext:clang_type_traits] __underlying_type: an enumeration's underlying
// type ([dcl.enum]/5, /7), in an alias template dependent on its parameter
// as libc++'s underlying_type spells it
enum Small : unsigned char { small_a = 200 };
enum class Scoped { a, b };
enum Plain { plain_a = -1, plain_b = 1 };
enum Big { big_a = 0x100000000LL };
template <class T> using underlying_t = __underlying_type(T);
template <class T> underlying_t<T> as_underlying(T v) { return (underlying_t<T>)v; }
__underlying_type(Small) s = small_a;
int main()
{
    underlying_t<Scoped> i = 7;
    underlying_t<Big> big = big_a;
    if (sizeof(s) != 1 || s != 200 || sizeof(i) != sizeof(int) || i != 7)
        return 1;
    if (sizeof(big) != 8 || big != 0x100000000LL)
        return 2;
    if (as_underlying(Scoped::b) != 1 || as_underlying(plain_a) != -1)
        return 3;
    underlying_t<Small> wraps = (underlying_t<Small>)300;
    return wraps == 44 ? 0 : 4;
}
