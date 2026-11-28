/* [temp.dep.candidate]/1: an operator declared after the template in an
   associated namespace of a dependent operand is found at instantiation;
   one beside it declared before, for other types, does not stand in its
   way. */
enum class A : int { a = 1 };
enum class B : int { b = 2 };
constexpr A operator|(A x, A y) { return A(int(x) | int(y)); }

struct Set {
    int bits;
    constexpr Set operator|(B x) const { return Set{bits | int(x)}; }
};

template <class T> int dependent(T x) { return int(x | x); }

/* a non-dependent expression of a template keeps its choice */
template <class T> int fixed(Set s) { return (s | B::b).bits + int(T(0)); }

constexpr B operator|(B x, B y) { return B(int(x) | int(y) | 8); }

int main()
{
    if (dependent(B::b) != 10)
        return 1;
    if (dependent(A::a) != 1)
        return 2;
    if (fixed<int>(Set{4}) != 6)
        return 3;
    return 0;
}
