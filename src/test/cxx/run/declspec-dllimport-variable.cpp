/* cxx-run-flags: -fms-extensions */
/* cxx-run-units: declspec-dllimport-variable-2.cpp */
/* [ext:microsoft_declspec] on clang's target __declspec(dllimport) is
   ignored (a warning), except that a variable declared with it and no
   initializer is a declaration, as with extern: the other unit defines it. */
#define check(e)                                                               \
    do {                                                                       \
        if (!(e))                                                              \
            __builtin_abort();                                                 \
    } while (0)

__declspec(dllimport) int imported_value;
__declspec(dllimport) extern int imported_extern;
__declspec(dllimport) int imported_function(int);

int main()
{
    check(imported_value == 7);
    check(imported_extern == 9);
    check(imported_function(2) == 4);
    return 0;
}
