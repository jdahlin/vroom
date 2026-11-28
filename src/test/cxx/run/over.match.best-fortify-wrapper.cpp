/* [over.match.best]/2: glibc's _FORTIFY_SOURCE=2 declares an overloadable
   pass_object_size wrapper beside the variadic open(); a call with two
   arguments is not ambiguous */
extern "C" int vf(const char *p, int flags, ...) { return p[0] + flags; }
extern "C" int vf_2(const char *p, int flags) { return p[0] + flags; }
extern "C" __attribute__((overloadable)) inline
    __attribute__((gnu_inline, always_inline)) int
    vf(const char *const __attribute__((pass_object_size(1))) p, int flags)
{
    return vf_2(p, flags);
}
int main() { return vf("a", 2) - 'a' - 2; }
