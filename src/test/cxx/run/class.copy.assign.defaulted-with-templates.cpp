// [class.copy.assign]: a class with defaulted assignments, bases whose
// assignments are user-provided, and an assignment template stays assignable.
int base_copies;
struct Base {
    Base &operator=(const Base &) { base_copies++; return *this; }
    Base &operator=(Base &&) = default;
};
struct Mid : Base {
    Mid &operator=(const Mid &) = default;
};
struct Opt : Mid {
    int v = 0;
    Opt &operator=(const Opt &) = default;
    Opt &operator=(Opt &&) = default;
    Opt &operator=(int n) { v = n; return *this; }
    template <class U> Opt &operator=(U &&) { v = -1; return *this; }
};
static_assert(__is_assignable(Opt &, const Opt &));

int main() {
    Opt a, b;
    b = 5;
    const Opt &c = b;
    a = c;
    return a.v != 5 || base_copies != 1;
}
