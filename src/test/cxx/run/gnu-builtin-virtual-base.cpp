/* [ext:clang_type_traits] __builtin_is_virtual_base_of(Base, Derived)
   (clang 20): Base is a virtual base class of Derived, direct or indirect
   ([class.mi]/4); a class is none of its own.  libc++'s
   std::is_virtual_base_of (C++26) is written with it. */
struct B {};
struct D : virtual B {};
struct E : D {};
struct F : B {};

static_assert(__builtin_is_virtual_base_of(B, D));
static_assert(__builtin_is_virtual_base_of(B, E));
static_assert(!__builtin_is_virtual_base_of(B, F));
static_assert(!__builtin_is_virtual_base_of(D, E));
static_assert(!__builtin_is_virtual_base_of(D, B));
static_assert(!__builtin_is_virtual_base_of(B, B));
static_assert(!__builtin_is_virtual_base_of(int, D));

template <class Base, class Derived>
inline constexpr bool virtual_base = __builtin_is_virtual_base_of(Base, Derived);
static_assert(virtual_base<B, E>);
static_assert(!virtual_base<F, E>);

int main()
{
    return 0;
}
