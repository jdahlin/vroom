/* [temp.expl.spec]: an explicit specialization of a function template
   declared with attributes (libc++'s _LIBCPP_HIDE_FROM_ABI) */
template <class T> int pick(T, int) { return 1; }
template <>
inline __attribute__((__visibility__("hidden"))) int
pick(long, int) {
    return 2;
}
template <class T> int other(T) { return 1; }
template <>
inline __attribute__((__visibility__("hidden"))) int other(char) {
    return 3;
}
/* a local declaration in the specialization's body */
struct R { int v; };
template <class T> R wide(T) { return {1}; }
template <>
inline __attribute__((__visibility__("hidden"))) R wide(long long x) {
    using I = int;
    I y = static_cast<I>(x);
    return {y};
}
int main() {
    if (wide(5LL).v != 5)
        return 4;
    if (pick(1, 0) != 1)
        return 1;
    if (pick(1L, 0) != 2)
        return 2;
    if (other('a') != 3)
        return 3;
    return 0;
}
