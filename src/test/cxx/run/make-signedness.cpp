/* [meta.trans.sign] through clang's __make_signed and __make_unsigned
   (libc++'s __make_unsigned_t) */
template <class A, class B> struct same { enum { value = 0 }; };
template <class A> struct same<A, A> { enum { value = 1 }; };

enum small : unsigned char { s0 };
enum class wide : long long { w0 };
enum plain { p0 = -1 };

static_assert(same<__make_unsigned(int), unsigned>::value, "");
static_assert(same<__make_unsigned(const long), const unsigned long>::value, "");
static_assert(same<__make_unsigned(char), unsigned char>::value, "");
static_assert(same<__make_signed(char), signed char>::value, "");
static_assert(same<__make_signed(unsigned short), short>::value, "");
static_assert(same<__make_signed(volatile unsigned long long),
                   volatile long long>::value, "");
static_assert(same<__make_signed(int), int>::value, "");
static_assert(same<__make_unsigned(wchar_t), unsigned>::value, "");
static_assert(same<__make_unsigned(char16_t), unsigned short>::value, "");
static_assert(same<__make_signed(char32_t), int>::value, "");
static_assert(same<__make_signed(char8_t), signed char>::value, "");
static_assert(same<__make_unsigned(small), unsigned char>::value, "");
static_assert(same<__make_signed(wide), long>::value, "");
static_assert(same<__make_unsigned(plain), unsigned>::value, "");
static_assert(same<__make_unsigned(__int128), unsigned __int128>::value, "");

template <class T> using make_unsigned_t = __make_unsigned(T);

template <class T> make_unsigned_t<T> as_unsigned(T v)
{
    return static_cast<make_unsigned_t<T>>(v);
}

/* two transformations of the same operand: two types, mangled with
   their names (u11__remove_cvIT_E, u15__make_unsignedIT_E) */
template <class T> __remove_cv(T) keep(T v) { return v; }
template <class T> __make_unsigned(T) widen(T v) { return v; }

int main()
{
    if (keep(-2) != -2 || widen(-1) != 4294967295u)
        return 3;
    if (as_unsigned(-1) != 4294967295u)
        return 1;
    __make_signed(unsigned char) c = -3;
    return c == -3 ? 0 : 2;
}
