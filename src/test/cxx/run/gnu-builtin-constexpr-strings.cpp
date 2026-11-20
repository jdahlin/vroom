/* [ext:gnu_library_builtins] the string and memory builtins clang
   evaluates in a constant expression ([expr.const]/5; clang's
   ExprConstant.cpp): __builtin_strlen, __builtin_strcmp, __builtin_strncmp,
   __builtin_memcmp, __builtin_memchr, __builtin_char_memchr,
   __builtin_wcslen, __builtin_wmemcmp, __builtin_wmemchr, with arguments
   that are arrays of constant objects.  libc++'s char_traits is written
   with them. */
typedef unsigned long size_t;

constexpr size_t length(const char *s)
{
    return __builtin_strlen(s);
}
constexpr int compare(const char *a, const char *b)
{
    return __builtin_strcmp(a, b);
}
constexpr int prefix(const char *a, const char *b, size_t n)
{
    return __builtin_strncmp(a, b, n);
}
constexpr int bytes(const char *a, const char *b, size_t n)
{
    return __builtin_memcmp(a, b, n);
}
constexpr const char *find(const char *s, int c, size_t n)
{
    return __builtin_char_memchr(s, c, n);
}
constexpr size_t wide_length(const wchar_t *s)
{
    return __builtin_wcslen(s);
}
constexpr int wide_compare(const wchar_t *a, const wchar_t *b, size_t n)
{
    return __builtin_wmemcmp(a, b, n);
}
constexpr const wchar_t *wide_find(const wchar_t *s, wchar_t c, size_t n)
{
    return __builtin_wmemchr(s, c, n);
}

static_assert(length("") == 0);
static_assert(length("abc") == 3);
static_assert(compare("abc", "abd") < 0);
static_assert(compare("b", "a") > 0);
static_assert(compare("same", "same") == 0);
static_assert(prefix("abcx", "abcy", 3) == 0);
static_assert(prefix("abcx", "abcy", 4) < 0);
static_assert(bytes("ab", "ac", 2) < 0);
static_assert(bytes("ab", "ac", 1) == 0);
static_assert(find("hello", 'l', 5) != nullptr);
static_assert(*find("hello", 'l', 5) == 'l');
static_assert(find("hello", 'z', 5) == nullptr);
static_assert(wide_length(L"wide") == 4);
static_assert(wide_compare(L"ab", L"ac", 2) < 0);
static_assert(wide_find(L"hello", L'l', 5) != nullptr);
static_assert(wide_find(L"hello", L'z', 5) == nullptr);

int main()
{
    char a[] = "run";
    if (length(a) != 3 || compare(a, "run") != 0 || prefix(a, "rum", 2) != 0)
        return 1;
    if (bytes(a, "ruo", 3) <= 0 || find(a, 'n', 3) != a + 2)
        return 2;
    return 0;
}
