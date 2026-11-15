// a function template-id not called (fn g = f<long>, &f<long>, take(f<char>), auto g = f<int>): one function
// template, its arguments all explicit: that instance, a function (doctest's TEST_CASE_TEMPLATE)
extern "C" int printf(const char*, ...);
typedef void (*fn)();
template <typename T> void func() { printf("%d\n", (int)sizeof(T)); }
void take(fn f) { f(); }
int A() { fn g = func<long>; g(); return 0; }
int B() { fn g = &func<long>; g(); return 0; }
int C() { take(func<char>); return 0; }
int D() { func<short>(); return 0; }
int E() { auto g = func<int>; g(); return 0; }
int main() { return A()+B()+C()+D()+E(); }
