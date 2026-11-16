// a non-template matching every argument exactly beats a template vcc cannot rank (its result type is not
// known before it is chosen): std::pow(2.0, -24.0) against libc++'s pow(_A1, _A2) -> __promote_t<_A1, _A2>
extern "C" int printf(const char*, ...);
template <class A, class B> struct promote;	/* (only declared: the template's result type stays unknown) */
inline double pw(double a, double b) { return a * 100 + b; }
template <class A, class B> typename promote<A, B>::type pw(A a, B b) { return 0; }
int main() { printf("%g\n", pw(2.0, -24.0)); }
