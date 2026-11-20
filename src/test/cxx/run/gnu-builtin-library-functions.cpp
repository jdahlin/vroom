/* [ext:gnu_library_builtins] the other C library functions' __builtin_
   forms, which clang declares at first use with the library's prototype:
   __builtin_bzero, __builtin_strcat, __builtin_malloc, ... (GCC manual
   6.63, "Both forms have the same type"). */
typedef unsigned long size_t;

int main()
{
    char a[16];
    __builtin_memset(a, 'x', sizeof a);
    __builtin_bzero(a, sizeof a);
    for (char c : a)
        if (c)
            return 1;
    __builtin_strcpy(a, "ab");
    __builtin_strcat(a, "cd");
    if (__builtin_strcmp(a, "abcd") || __builtin_strlen(a) != 4)
        return 2;
    __builtin_strncat(a, "efgh", 2);
    if (__builtin_strcmp(a, "abcdef"))
        return 3;
    void *m = __builtin_malloc(32);
    if (!m)
        return 4;
    __builtin_memset(m, 7, 32);
    void *n = __builtin_realloc(m, 64);
    if (!n || ((unsigned char *)n)[31] != 7)
        return 5;
    __builtin_free(n);
    void *z = __builtin_calloc(4, 4);
    if (((int *)z)[3] != 0)
        return 6;
    __builtin_free(z);
    if (__builtin_labs(-5L) != 5 || __builtin_llabs(-5LL) != 5)
        return 7;
    char b[8];
    __builtin_memcpy_inline(b, a, 8);
    if (b[0] != 'a')
        return 8;
    return 0;
}
