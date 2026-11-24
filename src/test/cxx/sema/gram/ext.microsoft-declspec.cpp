// [ext:microsoft_declspec] __declspec ( attribute-list ) among the
// decl-specifiers and in a class head (-fms-extensions): the grammar; the
// attributes' meaning is the analysis' (ext.gnu).
// RUN: %clang_cc1 -std=c++20 -fms-extensions -fsyntax-only %s
struct __declspec(novtable) A { virtual void f() = 0; };
__declspec(noinline) void g() {}
__declspec(align(16)) int aligned_variable;
__declspec(deprecated("old")) void h();
// On clang's target dllexport is ignored (a warning).
__declspec(dllexport) void exported();
struct __declspec(dllexport) Exported { void m(); };
__declspec(dllexport) int exported_variable = 1;
__declspec(restrict) void *allocates();
__declspec(noalias) void pure_of_memory(int *);
// dllimport (ignored on clang's target): a variable it declares without an
// initializer is a declaration, so it can be declared again and defined later.
__declspec(dllimport) int imported_variable;
__declspec(dllimport) extern int imported_extern;
__declspec(dllimport) int imported_then_defined;
int imported_then_defined;
__declspec(dllimport) int imported_with_initializer = 3;
__declspec(dllimport) void imported_function();
__declspec(dllimport) inline int imported_inline() { return 1; }
struct __declspec(dllimport) Imported { static int member; void m(); };
struct HasImported { __declspec(dllimport) static int member; };
