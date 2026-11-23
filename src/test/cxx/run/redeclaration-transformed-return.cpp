/* Function templates of one name whose return types are a dependent
   specialization in one and a type known only by how it is written
   (__remove_cv(T), decltype) in the other, as libc++'s <any>: comparing
   them for equivalence ([temp.over.link]/7) finds two overloads; this used
   to crash.  m is declared, not called (decltype's mangling is not
   done yet). */
#include <stdio.h>

template <class T> struct box {
    T value;
};

template <class T> box<T> h(int x) { return box<T>{x}; }
template <class T> __remove_cv(T) h(long x) { return x + 1; }
template <class T> __remove_cv(T) k(long x) { return x + 2; }
template <class T> box<T> k(int x) { return box<T>{x * 2}; }
template <class T> decltype(T()) m(long x) { return x + 3; }
template <class T> box<T> m(int x) { return box<T>{x * 3}; }

int main(void)
{
    int failures = 0;
    if (h<int>(4).value != 4 || h<const int>(4L) != 5)
        failures |= 1;
    if (k<int>(4).value != 8 || k<int>(4L) != 6)
        failures |= 2;
    printf("%d\n", failures);
    return failures;
}
