// RUN: %clang_cc1 -std=c++17 -fms-extensions -fsyntax-only %s
// [ext:microsoft_declspec]: __declspec(align(N)) is aligned's; empty_bases and
// layout_version change nothing here; a __declspec before a class-specifier
// is a decl-specifier, as __attribute__ is, not an attribute-specifier-seq.
__declspec(align(16)) int v;
__declspec(empty_bases) struct E { int x; };
__declspec(noinline) void f();
static_assert(alignof(decltype(v)) == alignof(int), "alignment is the variable's");
