// [dcl.init.list]/3.4 (CWG 2311): list-initialization of a class from a single
// prvalue of the same class type in C++17+ initializes the object directly
// with no copy/move constructor (e.g. __chrono/year_month_day.h:336).

struct S {
    int x;
    constexpr explicit S(int v) : x(v) {}
    S(const S &) = delete;
    S(S &&) = delete;
};

constexpr S make_s(int v) { return S{v}; }

int main()
{
    S a{make_s(42)};
    if (a.x != 42)
        return 1;
    return 0;
}
