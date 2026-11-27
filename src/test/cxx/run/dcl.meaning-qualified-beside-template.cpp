/* [dcl.meaning]: a qualified definition of a namespace function whose name a
   function template of the namespace has too. */
extern "C" int printf(const char *, ...);
namespace N {
int Make(int a, int b, bool c = true);
template <int M> int Make(int a, int b) { return Make(a, b, M > 0); }
}
int N::Make(int a, int b, bool c) { return a * 10 + b + c * 100; }
int main(){ printf("%d %d %d\n", N::Make(1, 2), N::Make<0>(3, 4), N::Make(5, 6, false)); }
