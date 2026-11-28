/* [over.built]/18-22: no built-in candidate has an enumeration as a
   compound assignment's left operand, so an operator function reached by an
   integral conversion of the right operand is called */
enum class Z : signed char { a = 0, b = 2 };
constexpr Z operator+(Z e, int o) { return static_cast<Z>(static_cast<int>(e) + o); }
template <class E> constexpr E &operator+=(E &e, int o)
{
    e = e + o;
    return e;
}
static const unsigned S = 3;
Z z = Z::b;
int main()
{
    z += S;
    return static_cast<int>(z) - 5;
}
