// [expr.const]: binding a const reference to a class prvalue inside a
// constexpr function (e.g. OverflowSafeInt::operator- in
// OpenTTD core/overflowsafe_type.hpp:208).

struct SafeInt {
    int v;
    constexpr SafeInt(int x) : v(x) {}
    constexpr SafeInt &operator-=(const SafeInt &rhs)
    {
        v -= rhs.v;
        return *this;
    }
    constexpr SafeInt operator-(int rhs) const
    {
        SafeInt res = *this;
        res -= SafeInt(rhs);
        return res;
    }
    constexpr bool operator==(const SafeInt &rhs) const { return v == rhs.v; }
};

static_assert(SafeInt(10) - 3 == SafeInt(7));

int main()
{
    SafeInt a(10);
    if (!(a - 3 == SafeInt(7)))
        return 1;
    return 0;
}
