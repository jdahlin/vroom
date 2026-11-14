// Members the layout rules needed (bench/cxx-layout.py checks json's; these parse): a function pointer
// in a nested declarator, a pointer to a forward-declared class, an anonymous union member.
namespace n {
struct A { void* (*const clone)(const void*); };
struct B { const bool b; };
struct T;
struct C { const T* const t; };
struct D { void* (*const clone)(const void*); void (*const d)(void*); const bool b; const T* const t;
  template <typename F> static const D* create() { return nullptr; } };
}
struct S { static constexpr int len = 4; union { char buf[len]; long l; }; };
