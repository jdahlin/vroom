/* [class.friend]/7: a friend first declared by its definition, with a
   parameter of an unnamed namespace's type (Catch's TablePrinter); and
   [temp.res]/5: static_cast<Q<T>::type> without typename. */
namespace
{
struct Break {
};
} // namespace

struct Printer {
    int rows = 0;
    friend Printer &operator<<(Printer &p, Break)
    {
        p.rows++;
        return p;
    }
};

enum class F : int { Newline = 2 };
template <class T, bool> struct impl;
template <class T> struct impl<T, true> {
    typedef int type;
};
template <class T> struct under : impl<T, true> {
};

int main()
{
    Printer p;
    p << Break() << Break();
    F f = F::Newline;
    return p.rows != 2 || static_cast<under<F>::type>(f) != 2;
}
