/* [class.copy.assign]/7: a class with a const member and assignment
   operators that take something else has a deleted implicit copy
   assignment: it is not assignable from its own type */
struct Nat {};
struct Str {
    Str() = default;
    Str &operator=(const Str &)
    {
        return *this;
    }
};
struct Pair {
    const Str first;
    int second = 0;
    Pair &operator=(const Nat &)
    {
        return *this;
    }
    Pair &operator=(Nat &&)
    {
        return *this;
    }
};
static_assert(!__is_assignable(Pair &, Pair), "");
static_assert(!__is_assignable(Pair &, const Pair &), "");
static_assert(__is_assignable(Pair &, Nat), "");
int main()
{
    Pair p;
    p = Nat();
    return p.second;
}
