// [class.base.init]/6: delegating constructor calling the trivial copy/move
// constructor (like libc++'s year_month_day(sys_days) : year_month_day(__from_days(...))).
struct Date {
    int y, m, d;
    Date() = default;
    constexpr Date(int y, int m, int d) : y(y), m(m), d(d) {}
    static constexpr Date from_days(int n) { return Date(2026, 11, n); }
    constexpr Date(int days) : Date(from_days(days)) {}
};

int main()
{
    int n = 26;
    Date dt(n);
    return (dt.y == 2026 && dt.m == 11 && dt.d == 26) ? 0 : 1;
}
