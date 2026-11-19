/* [class.static.data]/3, [dcl.ambig.res]/1: an out-of-class definition of a
   static data member whose parenthesized initializer names an enumerator as
   E::name ([basic.scope.enum]) is direct-initialization, not a function
   declarator (libc++'s <compare>) */
enum class E : int { a = 1, b = 2 };
enum U { u = 3 };
struct P {
    int v;
    explicit P(E e) : v((int)e) {}
    P(int x) : v(x) {}
    static const P less, more, other;
};
const P P::less(E::a);
const P P::more(E::b);
const P P::other(U::u);
int main() { return P::less.v + P::more.v + P::other.v == 6 ? 0 : 1; }
