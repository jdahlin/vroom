// [basic.scope.pdecl]/1: a constructor parameter type (Year) is not shadowed
// by a member function (Year()) declared later in the class when re-analysing
// the constructor's declarator for its body (like libc++'s year_month_day_last).
struct Year {
    int y;
};

struct Date {
    Year y_;
    constexpr Date(const Year &y) : y_{y} {}
    constexpr Year Year() const { return y_; }
};

int main()
{
    Date d(Year{2026});
    return d.Year().y == 2026 ? 0 : 1;
}
