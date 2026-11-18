/* Alias-declarations at namespace and block scope, and alias templates:
   a template-id naming one is the type substituted, in a variable's
   declaration, in a function template's parameter (deduction sees
   through it, mangling is the substituted type's) and in a class
   template's member ([dcl.typedef], [temp.alias]). */
#include <stdio.h>

using integer = int;
using cstr = const char *;

template <class T, int N> struct box {
    T a[N];
    int size() const { return N; }
};

template <class T> using ptr = T *;
template <class T> using quad = box<T, 4>;
template <int N> using ints = box<int, N>;
template <class T, int N = 2> using arr = box<T, N>;

namespace ns {
template <class T> using ref = T &;
using small = short;
}

template <class T> T load(ptr<T> p) { return *p; }
template <class T> int count(const quad<T> &q) { return q.size(); }
template <class T> void bump(ns::ref<T> r) { r += 1; }

template <class T> struct holder {
    ptr<T> p;
    T get() const { return *p; }
};

int main()
{
    int fail = 0;
    integer i = 41;
    cstr s = "abc";
    ptr<int> p = &i;
    if (*p != 41 || s[1] != 'b')
        fail |= 1;
    quad<char> q;
    if (sizeof q != 4 || count(q) != 4)
        fail |= 2;
    ints<3> b;
    if (sizeof b != 12 || b.size() != 3)
        fail |= 4;
    if (load(p) != 41)
        fail |= 8;
    bump<int>(i);
    if (i != 42)
        fail |= 16;
    holder<int> h = {&i};
    if (h.get() != 42)
        fail |= 32;
    arr<char, 8> pb;
    arr<short> ps;
    if (pb.size() != 8 || ps.size() != 2 || sizeof ps != 4)
        fail |= 64;
    {
        using local = long;
        local l = 5;
        ns::small sm = 2;
        if (sizeof l != 8 || l + sm != 7)
            fail |= 128;
    }
    printf("%d\n", fail);
    return fail;
}
