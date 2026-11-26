// [namespace.udir]/6: operator functions found in several namespaces are one
// overload set (Stockfish's tbprobe.cpp).
namespace S {
enum P { A, B, C };
enum Q { q0 = 5 };
int operator-(Q a, P b) { return int(a) - int(b) + 100; }
int operator-(Q a, Q b) { return int(a) - int(b) + 200; }
template <P Pt> int f() { return Pt - B; }
}
using namespace S;
namespace {
enum W { w0 = 3 };
inline int operator-(W d) { return -int(d) * 10; }
}
int main() {
    if (f<C>() != 1 || S::C - S::B != 1)
        return 1;
    if (q0 - C != 103 || q0 - q0 != 200)
        return 2;
    return -w0 != -30;
}
