/* [except.spec]/2: noexcept and noexcept(constant-expression) on
   function declarators; part of the function type since C++17 (P0012R1),
   mangled Do ([abi:mangling-type]) where the type is mangled, and a
   pointer to noexcept function converts to a pointer to function
   ([conv.fctptr]) */
static int base = 40;
int plain() noexcept { return 1; }
int conditional() noexcept(sizeof(int) == 4) { return base; }
int potentially() noexcept(false) { return 2; }
struct S {
    int v;
    int get() const noexcept { return v; }
    static int twice(int x) noexcept { return 2 * x; }
    ~S() noexcept {}
};
/* the parameter's type has the exception specification: in the mangled
   name, _Z4callPDoFivE */
int call(int (*f)() noexcept) { return f(); }
int call_any(int (*f)()) { return f(); }
void operator delete(void *, unsigned long) noexcept;

int main()
{
    int (*p)() noexcept = plain;
    int (*q)() = p;
    int (*r)() = conditional;
    S s = {5};
    if (call(plain) != 1 || call(p) != 1 || q() != 1 || r() != 40 ||
        call_any(plain) != 1 || call_any(potentially) != 2 ||
        s.get() != 5 || S::twice(3) != 6)
        return 1;
    return 0;
}
