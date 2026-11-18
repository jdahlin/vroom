/* Overloaded function templates: each template's deduction gives a
   candidate, a failed deduction none, and the best viable function is
   chosen by the arguments' conversions and then by partial ordering
   (T* over T, const T& over T&, T& over T&&, a fixed parameter over a
   pack, no pack over an empty one); head/tail recursive variadic
   templates; non-template functions of the name, which win a tie
   ([over.match.best]/2.4); the most specialized of several matching class template
   partial specializations ([temp.over], [over.match.best]/2.5,
   [temp.func.order], [temp.deduct.partial], [temp.class.order]). */
#include <stdio.h>

template <class T> int kind(T) { return 1; }
template <class T> int kind(T *) { return 2; }
template <class T> int kind(T **) { return 3; }

template <class T> int ref(T &) { return 1; }
template <class T> int ref(const T &) { return 2; }

template <class T> int fwd(T &) { return 1; }
template <class T> int fwd(T &&) { return 2; }

template <class T> int pair(T, T) { return 1; }
template <class T, class U> int pair(T, U) { return 2; }

template <class T> int conv(T, int) { return 1; }
template <class T> int conv(T, double) { return 2; }

template <class T> T sum(T t) { return t; }
template <class T, class... R> T sum(T t, R... r) { return t + sum(r...); }

template <class T> int count(T) { return 1; }
template <class T, class... R> int count(T, R... r) { return 1 + count(r...); }

template <class... A> int which(A... a) { return 1; }
template <class T, class... A> int which(T t, A... a) { return 2; }
template <class T, class U> int which(T, U) { return 3; }

template <class T> int arity(T) { return 1; }
template <class T> int arity(T, T) { return 2; }
template <class T> int arity(T, T, T) { return 3; }

int mixed(int) { return 0; }
int mixed(long) { return 3; }
template <class T> int mixed(T) { return 1; }
template <class T> int mixed(T *) { return 2; }

namespace n {
int only(double) { return 0; }
template <class T> int only(T) { return 1; }
} // namespace n

template <class T, class U> struct pick {
    static int get() { return 0; }
};
template <class T> struct pick<T, int> {
    static int get() { return 1; }
};
template <class T> struct pick<T *, int> {
    static int get() { return 2; }
};
template <class T> struct pick<T, T> {
    static int get() { return 3; }
};

template <int I, int J, class T> struct grid {
    static int get() { return 0; }
};
template <int I, int J> struct grid<I, J, int> {
    static int get() { return 1; }
};
template <int I> struct grid<I, I, int> {
    static int get() { return 2; }
};

int main()
{
    int failures = 0;
    int i = 0;
    int *p = &i;
    int **pp = &p;
    const int c = 1;
    if (kind(1) != 1 || kind(p) != 2 || kind(pp) != 3)
        failures |= 1 << 0;
    if (ref(i) != 1 || ref(c) != 2 || ref(3) != 2)
        failures |= 1 << 1;
    if (fwd(i) != 1 || fwd(3) != 2)
        failures |= 1 << 2;
    if (pair(1, 2) != 1 || pair(1, 2.0) != 2)
        failures |= 1 << 3;
    if (conv('a', 1) != 1 || conv('a', 1.0) != 2)
        failures |= 1 << 4;
    if (sum(1, 2, 3) != 6 || sum(4) != 4 || sum(1.5, 2) != 3.5)
        failures |= 1 << 5;
    if (count(1, 'a', 2.0, p) != 4)
        failures |= 1 << 6;
    if (which() != 1 || which(1) != 2 || which(1, 2) != 3 ||
        which(1, 2, 3) != 2)
        failures |= 1 << 7;
    if (arity(1) != 1 || arity(1, 2) != 2 || arity(1, 2, 3) != 3)
        failures |= 1 << 8;
    if (pick<char, long>::get() != 0 || pick<char, int>::get() != 1 ||
        pick<char *, int>::get() != 2 || pick<long, long>::get() != 3)
        failures |= 1 << 9;
    if (grid<1, 2, long>::get() != 0 || grid<1, 2, int>::get() != 1 ||
        grid<3, 3, int>::get() != 2)
        failures |= 1 << 10;
    if (mixed(1) != 0 || mixed(1L) != 3 || mixed(1.0) != 1 || mixed(p) != 2 ||
        mixed('a') != 1)
        failures |= 1 << 11;
    if (n::only(2) != 1 || n::only(2.0) != 0)
        failures |= 1 << 12;
    printf("%d\n", failures);
    return failures;
}
