/* [temp.local]/1: in an explicit specialization O<true> the
   injected-class-name O is a template-name too (libc++'s _OrImpl) */
struct T {
    static constexpr bool value = true;
    int x;
};
template <bool> struct O;
template <> struct O<false> {
    static int get(T t) { return t.x; }
};
template <> struct O<true> {
    static int get(T t) { return O<!T::value>::get(t) + 1; }
};
int main()
{
    T t = {4};
    return O<true>::get(t) - 5;
}
