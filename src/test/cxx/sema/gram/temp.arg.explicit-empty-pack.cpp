// [temp.arg.explicit]/4: a template parameter pack with no argument deduced or
// given is an empty pack.
template <typename... T> int count(int) { return sizeof...(T); }
template <typename... T, int N> int count2(int (&)[N]) { return sizeof...(T) + N; }
template <typename... T> struct S { static const int n = sizeof...(T); };
int a[2];
int x = count(1);
int y = count2(a);
static_assert(S<>::n == 0, "");
