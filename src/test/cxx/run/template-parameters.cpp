/* Default template-arguments and non-type template parameters: a class
   template's defaults, one naming an earlier parameter, a non-type
   parameter as an array bound and a value, negative and bool arguments,
   a function template's explicit non-type argument and default type
   argument ([temp.param], [temp.arg.nontype], [temp.arg]/4,
   [temp.deduct]/5). */
#include <stdio.h>

template <class T, int N = 4> struct array {
    T items[N];
    int size() const { return N; }
    T sum() const
    {
        T total = 0;
        for (int i = 0; i < N; i++)
            total += items[i];
        return total;
    }
};

template <class T = long, class U = T> struct duo {
    T first;
    U second;
};

template <int N> struct offset {
    int apply(int x) const { return x + N; }
};

template <bool B> struct flag {
    int value() const { return B ? 1 : 0; }
};

template <unsigned long N> struct big {
    unsigned long get() const { return N; }
};

template <int N> int scaled(int x) { return x * N; }

template <class T = int> T zero() { return 0; }

template <class T, int N> T nth(const array<T, N> &a, int i) { return a.items[i]; }

int main()
{
    int failures = 0;
    array<int> a = {{1, 2, 3, 4}};
    array<char, 2> c = {{5, 6}};
    if (a.size() != 4 || a.sum() != 10 || c.size() != 2 || c.sum() != 11 ||
        sizeof(array<double, 3>) != 24)
        failures |= 1;
    duo<> d = {1, 2};
    duo<char> e = {'a', 'b'};
    if (sizeof d != 16 || sizeof e != 2 || sizeof(duo<char, int>) != 8)
        failures |= 2;
    offset<-3> minus;
    offset<2 + 3> plus;
    if (minus.apply(10) != 7 || plus.apply(10) != 15)
        failures |= 4;
    flag<true> yes;
    flag<false> no;
    big<4000000000UL> huge;
    if (yes.value() != 1 || no.value() != 0 || huge.get() != 4000000000UL)
        failures |= 8;
    if (scaled<3>(5) != 15 || scaled<-2>(5) != -10 || zero() != 0 ||
        sizeof(zero<char>()) != 1)
        failures |= 16;
    if (nth<int, 4>(a, 2) != 3 || nth(c, 1) != 6 || nth(a, 3) != 4)
        failures |= 32;
    printf("%d\n", failures);
    return failures;
}
