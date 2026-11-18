/* [dcl.align]: alignment-specifiers in a class-head, of a constant
   (alignas(16)) and of a type (alignas(int): alignas(alignof(int))), the
   strictest of several; a class template's depends on its parameters and
   is taken in each specialization.  The alignment comes before the bases
   and members (a derived class keeps it), and the size is a multiple */
struct alignas(16) X {
    char c;
};
struct alignas(int) alignas(2) Z {};
struct Y : X {
    char d;
};
template <class T> struct alignas(alignof(T)) A {};
template <unsigned N> struct alignas(N) B {
    char c;
};

static_assert(alignof(X) == 16 && sizeof(X) == 16);
static_assert(alignof(Z) == 4 && sizeof(Z) == 4);
static_assert(alignof(Y) == 16 && sizeof(Y) == 32);
static_assert(alignof(A<double>) == 8 && sizeof(A<double>) == 8);
static_assert(alignof(B<32>) == 32 && sizeof(B<32>) == 32);

X x[2];
B<64> b;

int main()
{
    if (reinterpret_cast<unsigned long>(&x[1]) % 16 ||
        reinterpret_cast<unsigned long>(&b) % 64)
        return 1;
    return sizeof(Y) - 32;
}
