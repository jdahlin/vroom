// [class.friend]: a friend operator declared in the class, defined outside it
// ([namespace.memdef]/3); a defaulted friend == next to a function template of
// the same name ([class.compare.default], [temp.over]/1).
template <class T> bool operator==(T, long long);
struct P {
    int v;
    friend bool operator==(P x, P y) noexcept;
    friend int operator<=>(P x, P y);
    friend P operator+(P x, P y);
};
bool operator==(P x, P y) noexcept { return x.v == y.v; }
int operator<=>(P x, P y) { return x.v - y.v; }
P operator+(P x, P y) { return P{x.v + y.v}; }
struct Q {
    signed char v;
    friend constexpr bool operator==(Q, Q) noexcept = default;
};
static_assert(Q{1} == Q{1});
static_assert(!(Q{1} == Q{2}));
int main()
{
    P a{1}, b{2};
    if (a == b) return 1;
    if ((a + b).v != 3) return 2;
    if ((a <=> b) >= 0) return 3;
    return 0;
}
