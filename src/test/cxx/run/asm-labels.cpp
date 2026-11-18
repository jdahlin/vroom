// [ext:gnu_asm_labels]: an asm label is the symbol of a function or
// variable; a redeclaration may leave it out
extern "C" int printf(const char *, ...);
int counter __asm__("vcc_" "counter") = 3;
int bump(int by) __asm__("vcc_bump");
int bump(int by)
{
    counter += by;
    return counter;
}
long twice(long x) asm("vcc_twice") __attribute__((noinline));
long twice(long x) { return 2 * x; }
// a C library function renamed, as Darwin's __DARWIN_ALIAS does
extern "C" int renamed_puts(const char *) __asm__("puts");
int main()
{
    renamed_puts("asm labels");
    return bump(4) == 7 && twice(21) == 42 ? 0 : 1;
}
