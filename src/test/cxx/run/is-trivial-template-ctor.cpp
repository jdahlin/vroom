// __is_trivial and __is_pod of a class whose constructor template could be
// called without arguments ([ext:clang_type_traits])
struct A { template <class... T> A(T...) {} int x; };
struct B { B() = default; template <class... T> B(T...) {} int x; };
struct C { template <class T = int> C(T = T()) {} int x; };

struct E { template <bool B = true> E() {} int x; };

int main()
{
    int r = __is_trivial(A) + 4 * __is_trivial(C) +
            8 * __is_pod(A) + 16 * __is_pod(B) + 64 * __is_trivial(E);
    __builtin_printf("%d\n", r);
    return 0;
}
