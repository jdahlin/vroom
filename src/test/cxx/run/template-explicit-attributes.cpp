/* A GNU attribute in an explicit instantiation, as libc++ writes
   extern template _LIBCPP_EXPORTED_FROM_ABI ...: before the declaration
   or after the class-key, in a definition and in a declaration
   ([temp.explicit]; [ext:gnu_attributes]).  The definitions are emitted
   like those without an attribute. */
#include <stdio.h>

#define EXPORT __attribute__((__visibility__("default")))

template <class T> T twice(T x) { return x + x; }
template <class T> struct box {
    T value;
    T get() const { return value; }
    void set(T v) { value = v; }
};

template EXPORT int twice<int>(int);
extern template EXPORT short twice<short>(short);
template class EXPORT box<int>;
extern template class EXPORT box<short>;
template EXPORT void box<char>::set(char);
template EXPORT char box<char>::get() const;

int main()
{
    box<int> a;
    a.set(3);
    box<char> c;
    c.set('a');
    int failed = 0;
    if (twice(21) != 42 || a.get() != 3 || c.get() != 'a')
        failed = 1;
    printf("%d\n", failed);
    return failed;
}
