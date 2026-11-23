/* [expr.sizeof]/1 and [basic.fundamental]: the size of a type-id's object,
   [expr.sizeof]/2: a reference's is its referenced type's; [expr.alignof]/1,
   /3 (reference), /4 (array: the element's).  The sizes are the Itanium
   ABI's ([abi:layout]): empty class 1, data member pointer 8, member
   function pointer 16, enumeration the underlying type's. */
struct Empty {};
struct Two { char a; short b; };
struct Base { int x; };
struct Derived : Base { char y; };
struct Virt : virtual Base { };
struct V { virtual void f(); };
struct NUA { [[no_unique_address]] Empty e; int i; };
struct Bits { unsigned a : 3; unsigned b : 20; char c; };
union U { char c[5]; int i; };
enum E1 { e1 };
enum class E2 : char { e2 };
enum E3 : long { e3 };
enum class E4 { e4 };
struct S { int m; double d; void f(); };

static_assert(sizeof(char) == 1 && sizeof(bool) == 1);
static_assert(sizeof(short) == 2 && sizeof(int) == 4 && sizeof(long) == 8);
static_assert(sizeof(long long) == 8 && sizeof(void *) == 8);
static_assert(sizeof(float) == 4 && sizeof(double) == 8);
static_assert(sizeof(decltype(nullptr)) == 8);
static_assert(sizeof(wchar_t) == 4 && sizeof(char16_t) == 2 && sizeof(char8_t) == 1);
static_assert(sizeof(int &) == 4 && sizeof(int &&) == 4 && sizeof(Two &) == 4);
static_assert(sizeof(double &) == 8 && sizeof(Empty &) == 1);
static_assert(sizeof(int[5]) == 20 && sizeof(int[2][3]) == 24);
static_assert(sizeof(int (&)[5]) == 20);
static_assert(sizeof(int *[3]) == 24 && sizeof(int (*)[3]) == 8);
static_assert(sizeof(Empty) == 1 && sizeof(Empty[3]) == 3);
static_assert(sizeof(Two) == 4 && sizeof(Derived) == 8);
static_assert(sizeof(Virt) == 16 && sizeof(V) == 8);
static_assert(sizeof(NUA) == 4);
static_assert(sizeof(Bits) == 4);
static_assert(sizeof(U) == 8);
static_assert(sizeof(E1) == 4 && sizeof(E2) == 1 && sizeof(E3) == 8 && sizeof(E4) == 4);
static_assert(sizeof(int S::*) == 8 && sizeof(void (S::*)()) == 16);
static_assert(sizeof(void (S::*)(int) const) == 16);
static_assert(sizeof(void (*)()) == 8);
static_assert(sizeof(const volatile int) == 4);
static_assert(sizeof(sizeof(int)) == sizeof(unsigned long));
static_assert(alignof(char) == 1 && alignof(short) == 2 && alignof(int) == 4);
static_assert(alignof(double) == 8 && alignof(long double) == 16);
static_assert(alignof(int &) == 4 && alignof(double &&) == 8);
static_assert(alignof(int[7]) == 4 && alignof(double[2][2]) == 8);
static_assert(alignof(int[]) == 4);
static_assert(alignof(Empty) == 1 && alignof(Two) == 2 && alignof(Virt) == 8);
static_assert(alignof(int S::*) == 8 && alignof(void (S::*)()) == 8);
static_assert(alignof(E2) == 1 && alignof(E3) == 8);
static_assert(__alignof__(int) == 4 && _Alignof(long) == 8);
static_assert(__alignof(Two) == 2);
static_assert(__is_same(decltype(sizeof(int)), unsigned long));
static_assert(__is_same(decltype(alignof(int)), unsigned long));

int main()
{
    auto closure = [] {};
    int x = 0;
    auto capture = [x] { return x; };
    struct Local { char c[3]; };
    struct { short s; char t; } anon;
    if (sizeof(closure) != 1 || sizeof(capture) != 4 || sizeof(Local) != 3 ||
        sizeof(anon) != 4 || alignof(decltype(anon)) != 2)
        return 1;
    return 0;
}
