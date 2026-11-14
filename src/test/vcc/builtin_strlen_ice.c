/* __builtin_strlen("literal") is an integer constant expression (clang), so
 * the kernel's __is_constexpr is true for it and FORTIFY's strlen() picks it
 * over __fortify_strlen() in a static initializer (zynqmp-debug.c's
 * PM_API table: "initializer element is not a compile-time constant"). */
#define __is_constexpr(x) (sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))
unsigned long __fortify_strlen(const char *p);
#define my_strlen(p) __builtin_choose_expr(__is_constexpr(__builtin_strlen(p)), __builtin_strlen(p), __fortify_strlen(p))
_Static_assert(__is_constexpr(__builtin_strlen("ABCD")), "ICE");
_Static_assert(__is_constexpr(__builtin_strlen("a\0b") + 0), "ICE with a NUL");
struct api { int id; char name[16]; char len; };
static struct api apis[] = { { 1, "FORCE_DOWN", my_strlen("FORCE_DOWN") }, { 2, "WAKE", my_strlen("WAKE") } };
enum { LEN = __builtin_strlen("abc") };
static int arr[__builtin_strlen("abcde")];

unsigned long __fortify_strlen(const char *p) { (void)p; return 99; }

int main(void)
{
    if (apis[0].len != 10 || apis[1].len != 4) return 1;
    if (LEN != 3 || sizeof arr != 5 * sizeof(int)) return 2;
    return 0;
}
