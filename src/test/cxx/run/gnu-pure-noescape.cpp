/* [dcl.attr.grammar]/4: libc++'s __hash_memory (__functional/hash.h):
   [[gnu::pure]] and [[gnu::const]] before the declaration, default
   visibility, and clang's noescape on a parameter ([dcl.fct]/3), none of
   which changes the code vcc generates */
typedef unsigned long size_t;
[[__gnu__::__pure__]] __attribute__((__visibility__("default"))) size_t
hash_memory(__attribute__((noescape)) const void *, size_t) noexcept;
[[gnu::const]] int twice(int) noexcept;
size_t hash_memory(__attribute__((noescape)) const void *p, size_t n) noexcept
{
    const unsigned char *s = (const unsigned char *)p;
    size_t h = 0;
    for (size_t i = 0; i < n; i++)
        h = h * 31 + s[i];
    return h;
}
int twice(int x) noexcept { return 2 * x; }
int main()
{
    return hash_memory("ab", 2) == 97 * 31 + 98 && twice(21) == 42 ? 0 : 1;
}
