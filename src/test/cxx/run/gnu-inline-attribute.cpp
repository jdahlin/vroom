/* [ext:gnu_attributes] gnu_inline: "In C++, this attribute does not depend
   on extern in any way, but it still requires the inline keyword": the
   definition is used only for inlining, never emitted, so at -O0 the call
   is to the C library's abs (glibc's __extern_inline in <stdlib.h>) */
extern "C" {
extern __inline __attribute__((__gnu_inline__)) int abs(int x)
{
    if (x < 0)
        return -x;
    return x;
}
}
int main() { return abs(-3) - 3; }
