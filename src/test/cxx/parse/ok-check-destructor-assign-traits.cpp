// trivially destructible and trivially/nothrow assignable for classes that are not POD for
// layout (libc++'s optional<T>: its bases are picked by these traits; Skia's optional<SkMatrix>)
struct M { int x; ~M(); };
struct T { int x; };
struct V { virtual ~V(); };
struct H { M m; };
struct E { ~E() = default; int x; };
class Priv { int a; float m[9]; public: constexpr Priv() : a(), m{} {} };
struct A { A &operator=(const A &); };
static_assert(!__is_trivially_destructible(M));
static_assert(!__is_trivially_destructible(V));
static_assert(__is_trivially_destructible(E));
static_assert(__is_trivially_destructible(T));
static_assert(__is_nothrow_assignable(M &, M &&));
static_assert(__is_trivially_assignable(Priv &, Priv &&));
static_assert(__is_nothrow_assignable(Priv &, const Priv &));
static_assert(!__is_trivially_assignable(A &, const A &));
