/* [ext:overloadable] glibc's fortify declarations (-D_FORTIFY_SOURCE=2): a
   static overloadable definition beside the library's extern "C" function
   must not export the plain name (a second unit's would collide with it) */
/* cxx-run-local-names-differ: clang names the local one _ZL4atoi... */
extern "C" {
extern int atoi(const char *) noexcept;
static __attribute__((__unused__)) __attribute__((__overloadable__)) int
atoi(const char *const s __attribute__((__pass_object_size__(1)))) noexcept
{
    return s[0] - '0';
}
}

int main()
{
    return atoi("3") - 3;
}
