/* [ext:gnu_attributes] libc++'s _LIBCPP_HIDE_FROM_ABI in a class-head:
   visibility("hidden") (its members' symbols), abi_tag (its name's,
   [abi:mangle.abi-tag]) and exclude_from_explicit_instantiation
   (__compare_three_way_result's primary template) */
#define HIDE                                                                   \
    __attribute__((visibility("hidden")))                                      \
    __attribute__((__exclude_from_explicit_instantiation__))                   \
    __attribute__((__abi_tag__("ne210000")))

template <class, class, class> struct HIDE result {};
template <class T> struct HIDE box {
    T v;
    T get() { return v; }
};
struct HIDE S {
    int v;
    int get() { return v; }
};

int use(S s, result<int, int, int>) { return s.get(); }
long take(box<long> b) { return b.get(); }

int main()
{
    return use(S{4}, result<int, int, int>{}) + (int)take(box<long>{3}) - 7;
}
