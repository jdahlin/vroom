// [meta.rel] is_convertible with an abstract class: no object of it is
// returned, so To abstract is false, a reference or pointer to it is fine.
struct Abs { virtual void f() = 0; };
struct Impl : Abs { void f() override {} };
static_assert(!__is_convertible(Abs, Abs));
static_assert(!__is_convertible(Impl, Abs));
static_assert(__is_convertible(Impl &, Abs &));
static_assert(__is_convertible(Impl *, Abs *));
