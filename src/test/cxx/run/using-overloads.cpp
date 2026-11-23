// [namespace.udecl]/1: a using-declarator naming an overload set introduces
// all of its declarations; a call does overload resolution over them
namespace m {
int f(int x) { return x + 1; }
long f(long x) { return x + 2; }
double f(double x) { return x * 2; }
template <class T> T g(T x) { return x; }
template <class T> T g(T x, T y) { return x + y; }
}
namespace n { using m::f; using m::g; }
using n::f;
// joining the set a name already denotes: a function, a function template
namespace p {
int h(char) { return 7; }
template <class T> int k(T *) { return 8; }
}
namespace q {
template <class T> int h(T) { return 9; }
int h(short) { return 10; }
template <class T> int k(T) { return 11; }
}
namespace p { using q::h; using q::k; }
int joined()
{
    int i = 0;
    return p::h('a') == 7 && p::h(1) == 9 && p::h((short)1) == 10 &&
           p::k(&i) == 8 && p::k(1) == 11;
}
int main()
{
    if (f(1) != 2 || f(2L) != 4 || f(1.5) != 3.0) return 1;
    if (n::f(3) != 4 || n::g(3) != 3 || n::g(1, 2) != 3) return 2;
    if (n::g(1.5, 1.0) != 2.5) return 3;
    if (!joined()) return 4;
    return 0;
}
