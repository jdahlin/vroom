/* [class.default.ctor]/4: the implicit default constructor of a class with
   no members calls its bases' constructors; at namespace scope it was taken
   to do nothing. */
extern "C" int printf(const char *, ...);
static int calls;
struct User {
    User() { calls++; }
};
struct Derived : User {};
struct Twice : Derived {};
struct Valued {
    int v;
    constexpr Valued() : v(5) {}
};
struct FromValued : Valued {};
struct Empty {
    constexpr Empty() {}
};
struct FromEmpty : Empty {};
struct Initialized {
    int v = 7;
};
struct FromInitialized : Initialized {};

Derived derived;
Twice twice;
FromValued from_valued;
FromEmpty from_empty;
FromInitialized from_initialized;

int main()
{
    printf("%d %d %d\n", calls, from_valued.v, from_initialized.v);
}
