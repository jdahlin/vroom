/* Member functions of class templates defined outside the class, before
   and after the class's specializations are used; members of a partial
   specialization defined outside it; a class template declared before
   its definition and named meanwhile as a pointer's type; default
   template-arguments of that first declaration ([temp.class],
   [temp.mem.func], [temp.class.spec.mfunc], [temp.param]/10); function
   templates declared, called, then defined ([temp.fct], [temp.point]/7). */
#include <stdio.h>

template <class T, int N = 2> struct counted;

counted<long> *first_use;

template <class T> struct box {
    T value;
    void set(T v);
    T get() const;
    T *address();
    int twice() const;
};

template <class T> struct box<T *> {
    T *pointer;
    T load() const;
};

template <class T> void box<T>::set(T v) { value = v; }

template <class T> T box<T>::get() const { return value; }

box<int> early;

template <class T> T *box<T>::address() { return &value; }

template <class T> int box<T>::twice() const { return 2 * (int)value; }

template <class T> T box<T *>::load() const { return *pointer; }

template <class T, int N> struct counted {
    T items[N];
    int size() const;
    T sum() const;
};

template <class T, int N> int counted<T, N>::size() const { return N; }

template <class T, int N> T counted<T, N>::sum() const
{
    T s = 0;
    for (int i = 0; i < N; i++)
        s += items[i];
    return s;
}

template <class T> T next(T);
template <class T> constexpr T scaled(T, int);

int use_next() { return next(41); }

template <class T> T next(T v) { return v + 1; }
template <class T> constexpr T scaled(T v, int k) { return v * k; }
static_assert(scaled(3, 4) == 12, "defined after its declaration");

int main()
{
    int failures = 0;
    early.set(4);
    if (early.get() != 4 || *early.address() != 4 || early.twice() != 8)
        failures |= 1;
    box<double> d;
    d.set(2.5);
    if (d.get() != 2.5 || d.twice() != 5)
        failures |= 2;
    int x = 7;
    box<int *> p = {&x};
    if (p.load() != 7)
        failures |= 4;
    counted<long> c = {{3, 4}};
    first_use = &c;
    if (first_use->size() != 2 || first_use->sum() != 7)
        failures |= 8;
    counted<int, 3> e = {{1, 2, 3}};
    if (e.size() != 3 || e.sum() != 6)
        failures |= 16;
    if (use_next() != 42 || next(1.5) != 2.5)
        failures |= 32;
    printf("%d\n", failures);
    return failures;
}
