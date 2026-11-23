// [ext:microsoft_declspec] __declspec ( attribute-list ) among the
// decl-specifiers and in a class head (-fms-extensions): the grammar; the
// attributes' meaning is the analysis' (ext.gnu).
// RUN: %clang_cc1 -std=c++20 -fms-extensions -fsyntax-only %s
struct __declspec(novtable) A { virtual void f() = 0; };
__declspec(noinline) void g() {}
__declspec(align(16)) int aligned_variable;
__declspec(deprecated("old")) void h();
