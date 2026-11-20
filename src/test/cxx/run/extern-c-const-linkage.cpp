/* cxx-run-units: extern-c-const-linkage-2.cpp */
/* [dcl.link]/7: "A declaration directly contained in a linkage-specification
   is treated as if it contains the extern specifier for the purpose of
   determining the linkage of the declared name and whether it is a
   definition": a const variable of `extern "C" const int x = 1;` has
   external linkage and C language linkage ([dcl.link]/5: the name is not
   mangled); inside braces the declaration is not so treated, and a const
   variable keeps the internal linkage of [basic.link]/3.2; `static` stays
   internal.  The other unit refers to the external ones by their C names. */

#define check(e)                                                               \
    do {                                                                       \
        if (!(e))                                                              \
            __builtin_abort();                                                 \
    } while (0)

extern "C" const int x = 1;
extern "C" {
const int y = 2;
int w = 5;
}
extern "C" {
static const int z = 3;
}
extern "C" constexpr int c = 4;
extern "C" const int arr[3] = {7, 8, 9};
extern "C" __attribute__((visibility("hidden"))) const unsigned char tab[4] = {
    1, 2, 3, 4};
namespace n
{
extern "C" const int in_ns = 6;
}

int other();

int main()
{
    check(x == 1 && y == 2 && z == 3 && c == 4 && w == 5);
    check(arr[2] == 9 && tab[3] == 4 && n::in_ns == 6);
    check(other() == 1 + 4 + 9 + 4 + 6 + 5);
    return 0;
}
