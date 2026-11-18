/* Partial and explicit specializations of class templates: one for
   pointers, one for a const type, one fixing a non-type argument, one
   deducing two parameters from one argument, an explicit specialization,
   and member functions of each ([temp.spec.partial],
   [temp.spec.partial.match], [temp.expl.spec]). */
#include <stdio.h>

template <class T> struct traits {
    int kind() const { return 0; }
    int size() const { return sizeof(T); }
};
template <class T> struct traits<T *> {
    int kind() const { return 1; }
    int size() const { return sizeof(T); }
};
template <class T> struct traits<const T> {
    int kind() const { return 2; }
};
template <> struct traits<bool> {
    int kind() const { return 3; }
    long extra;
};

template <class T, int N> struct vec {
    T items[N];
    int count() const { return N; }
};
template <class T> struct vec<T, 0> {
    int count() const { return 0; }
};

template <class A, class B> struct same {
    int value() const { return 0; }
};
template <class A> struct same<A, A> {
    int value() const { return 1; }
};

template <class T> struct holder {
    T held;
};
template <class T> struct holder<holder<T>> {
    T inner;
    int depth() const { return 2; }
};

int main()
{
    int failures = 0;
    traits<int> i;
    traits<double *> p;
    traits<const int> c;
    traits<bool> b;
    if (i.kind() != 0 || p.kind() != 1 || c.kind() != 2 || b.kind() != 3 ||
        i.size() != 4 || p.size() != 8 || sizeof(traits<bool>) != 8)
        failures |= 1;
    vec<int, 3> v;
    vec<int, 0> empty;
    if (v.count() != 3 || empty.count() != 0 || sizeof v != 12 ||
        sizeof empty != 1)
        failures |= 2;
    same<int, int> yes;
    same<int, long> no;
    if (yes.value() != 1 || no.value() != 0)
        failures |= 4;
    holder<holder<char>> h = {'x'};
    if (h.depth() != 2 || h.inner != 'x' || sizeof h != 1)
        failures |= 8;
    printf("%d\n", failures);
    return failures;
}
