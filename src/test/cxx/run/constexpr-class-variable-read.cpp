/* cxx-run-flags: -std=c++20 */
/* [conv.lval]: a constexpr variable of class type read as a value (copied)
   is read from its definition, not folded to a scalar. */
struct order {
    signed char v;
    static const order less, equal;
};
inline constexpr order order::less{-1}, order::equal{0};
constexpr order pick(bool lower) {
    if (lower)
        return order::less;
    return order::equal;
}
int main(int argc, char **)
{
    order a = order::less;
    order b = pick(argc > 5);
    const order *p = &order::less, *q = &order::equal;
    return (a.v + 1) + b.v + (p == q);
}
