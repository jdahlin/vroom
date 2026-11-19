/* clang's enable_if with a true condition, as libc++'s
   _LIBCPP_PREFERRED_OVERLOAD in <string.h>: the attributed function is a
   declaration of its own beside the C function that differs only in the
   return type, and overload resolution prefers it */
#define PREFERRED __attribute__((__enable_if__(true, "")))

extern "C" char *strchr(const char *, int);

extern "C++" {
inline PREFERRED const char *strchr(const char *s, int c)
{
    return __builtin_strchr(s, c);
}
inline __attribute__((__abi_tag__("t1"))) PREFERRED char *strchr(char *s, int c)
{
    return __builtin_strchr(s, c);
}
}

int main()
{
    const char *p = "abc";
    char b[4] = "xyz";
    const char *q = strchr(p, 'b');
    char *r = strchr(b, 'z');
    *r = 'w';
    return (q - p) + (r - b) + (b[2] == 'w') - 4;
}
