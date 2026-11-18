/* Template-ids after :: and in expressions: a static member function of a
   specialization called through its qualified name, an enumerator of a
   specialization, a template of a namespace named after ::, the explicit
   type conversion box<int>(...), and a static member reached through an
   object ([temp.names], [expr.type.conv], [expr.ref]). */
#include <stdio.h>

template <class T> struct traits {
    static int size() { return sizeof(T); }
    static T twice(T v) { return v + v; }
    enum { bits = sizeof(T) * 8 };
};

namespace util {
template <class T> struct limits {
    static T low() { return T(1) << 2; }
};
} // namespace util

template <class T> struct box {
    T value;
    box(T v) : value(v) {}
    T get() const { return value; }
};

static int counter;
struct counted {
    static int bump(int by) { return counter += by; }
};
static counted &make()
{
    static counted c;
    counter += 100;
    return c;
}

int main()
{
    int fail = 0;
    if (traits<int>::size() != 4 || traits<char>::size() != 1)
        fail |= 1;
    if (traits<long>::twice(21) != 42)
        fail |= 2;
    if (traits<short>::bits != 16)
        fail |= 4;
    if (util::limits<int>::low() != 4 || ::util::limits<long>::low() != 4)
        fail |= 8;
    if (box<int>(7).get() != 7)
        fail |= 16;
    traits<double> t;
    if (t.size() != 8)
        fail |= 32;
    if (make().bump(1) != 101 || counter != 101)
        fail |= 64;
    printf("%d\n", fail);
    return fail;
}
