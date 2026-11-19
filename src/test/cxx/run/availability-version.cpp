/* [ext:clang_availability] a version in clang's availability attribute is
   a pp-number that is no literal (10.12.1, [lex.ppnumber]/2): accepted
   there, as in the macOS SDK's <string.h>. */
int twice(int x)
    __attribute__((availability(macos, introduced = 10.12.1, deprecated = 11,
                                message = "use thrice")));
int twice(int x)
{
    return 2 * x;
}

int main()
{
    return twice(21) == 42 ? 0 : 1;
}
