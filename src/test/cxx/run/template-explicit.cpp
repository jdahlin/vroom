/* Explicit instantiations and explicit specializations of templates: a
   function template's specialization instantiated by its template-id or
   with its arguments deduced, inline and constexpr ones (emitted all the
   same), a class template's specialization with its member functions
   (those used and those not), one member function of another, an
   explicit instantiation declaration followed by the definition, and
   explicit specializations of function templates used in place of an
   instantiation, declared before their definition, constexpr
   ([temp.explicit], [temp.expl.spec]). */
#include <stdio.h>

template <class T> T twice(T x) { return x + x; }
template <class T> inline T thrice(T x) { return x * 3; }
template <class T> constexpr T square(T x) { return x * x; }

template int twice<int>(int);
template long twice(long);
template int thrice(int);
template long square<long>(long);

template <class T> struct box {
    T value;
    T get() const { return value; }
    void set(T v);
    T unused() const { return value + 1; }
};
template <class T> void box<T>::set(T v) { value = v; }

template struct box<int>;
template void box<long>::set(long);

extern template short twice(short);
extern template struct box<char>;
template short twice(short);
template struct box<char>;

template <class T> T pick(T x) { return x; }
template <> int pick<int>(int x) { return x + 100; }
template <> long pick(long x) { return x + 200; }
template <> short pick<short>(short);

template <class T> constexpr T scale(T x) { return x * 2; }
template <> constexpr int scale(int x) { return x * 5; }
static_assert(scale(3) == 15);
static_assert(scale(3L) == 6);

int use_pick_short() { return pick((short)1); }
template <> short pick<short>(short x) { return x + 300; }

int main()
{
    volatile int four = 4;
    int failed = 0;
    if (twice(21) != 42 || twice(4L) != 8 || twice((short)3) != 6)
        failed |= 1;
    if (thrice(5) != 15 || square(6L) != 36)
        failed |= 2;
    box<int> b;
    b.set(7);
    if (b.get() != 7)
        failed |= 4;
    box<long> l;
    l.set(9);
    box<char> c;
    c.set('a');
    if (l.value != 9 || c.get() != 'a')
        failed |= 8;
    if (pick(1) != 101 || pick(2L) != 202 || pick('x') != 'x')
        failed |= 16;
    if (use_pick_short() != 301)
        failed |= 32;
    if (scale(four) != 20 || scale((long)four) != 8)
        failed |= 64;
    printf("%d\n", failed);
    return failed;
}
