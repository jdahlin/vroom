/* [impl.clang] __builtin_char_memchr returns char *: a function template
   returning _Tp * from it, for a non-const _Tp (libc++'s __constexpr_memchr). */
typedef decltype(sizeof 0) size_t;
template <class T>
T *find(T *s, int v, size_t n)
{
    return __builtin_char_memchr(s, v, n);
}

int main()
{
    char w[] = "abc";
    char *p = find(w, 'b', 3);
    if (p != w + 1)
        return 1;
    const char *c = find((const char *)w, 'c', 3);
    if (c != w + 2)
        return 2;
    return 0;
}
