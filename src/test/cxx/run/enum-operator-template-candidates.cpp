/* [over.match.oper]/3.2: with no class operand, a function template's
   specialization is a candidate only if it takes the enumeration itself
   (libc++'s operator+(difference_type, iterator) is none) */
template <class T> struct I {
    using difference = long;
};
struct W {
    template <class T> W(T) {}
};
template <class T> int operator+(typename I<T>::difference, W) { return 100; }
template <class T = int> int operator<<(long, W) { return 100; }

struct R {
    enum { s = 1, f = 0, b = 1 };
};
enum E { e = 4 };
template <class T> int operator-(T, E) { return 50; }

template <class Q> int g() { return (Q::s + Q::f + Q::b) << 4; }

int main() { return g<R>() != 32 || e - e != 50 || 1 + e != 5; }
