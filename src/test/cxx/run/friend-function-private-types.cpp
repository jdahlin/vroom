/* cxx-run-flags: -std=c++20 */
/* [class.access]/6: a friend function's declaration outside the class may
   name the class's private members in its return and parameter types */
template<class E> struct M { E e; constexpr M(E x) : e(x) {} constexpr M operator|(M o) const { return M(E(int(e)|int(o.e))); } };
class F {
    enum class S : int { a = 1, b = 2 };
    friend constexpr M<S> operator|(S, S);
    friend constexpr M<S> operator~(S);
public:
    static int g();
};
[[maybe_unused]] constexpr M<F::S> operator|(F::S a, F::S b) { return M<F::S>(a) | b; }
[[maybe_unused]] constexpr M<F::S> operator~(F::S a) { return M<F::S>(a); }
int F::g() { return int((S::a | S::b).e); }
int main() { return F::g() != 3; }
