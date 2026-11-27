/* [meta.unary.prop], [dcl.init]/17.6.2.2: a parenthesized aggregate
   initialization leaves class members to value-initialization
   (optional<SkPngEncoderBase::TargetInfo>). */
extern "C" int printf(const char *, ...);
struct N {};
struct O { constexpr O() {} constexpr O(N) {} };
struct U { U() noexcept {} int *p = nullptr; };
template <class X> struct V { constexpr V() noexcept : p(nullptr) {} int *p; };
struct Th { Th() {} };
struct No { No(int) {} };
struct A { O a; V<int> e; unsigned long n; };
struct B { O a; Th t; };
struct C { O a; No x; };
struct D { O a; U u[2]; };
int main() {
    printf("%d %d %d %d %d %d %d %d\n", __is_constructible(A, N), __is_nothrow_constructible(A, N), __is_constructible(B, N), __is_nothrow_constructible(B, N), __is_constructible(C, N), __is_nothrow_constructible(C, N), __is_constructible(D, N), __is_nothrow_constructible(D, N));
}
