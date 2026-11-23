// [namespace.udecl]/1 (C++17 P0195): a using-declaration with a pack
// expansion brings the member of each base of a pack into the class; the
// "overloaded" visitor idiom, built by an aggregate with base classes
// ([dcl.init.aggr]/1) and class template argument deduction through a
// deduction guide ([over.match.class.deduct]/1)
template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

struct A { int operator()(int x) const { return x + 1; } };
struct B { int operator()(char *p) const { return p ? 10 : 20; } };
struct C { int operator()(double d) const { return static_cast<int>(d) + 100; } };

// the visitor passed on to a function template
template <class V> int apply(V &&v)
{
    return v(1) * 100 + v(2.5);
}

int main()
{
    // (volatile: clang folds the calls of a captureless lambda, which is
    // constexpr, where it can, and defines no function for them)
    volatile int four = 4, one_ = 1, three = 3;
    volatile long long_one = 1;
    volatile double one_and_half = 1.5;
    const char *volatile text = "x";
    char c = 0;
    overloaded<A> o1{A{}};
    if (o1(1) != 2)
        return 1;
    overloaded<A, B> o2{A{}, B{}};
    if (o2(1) != 2 || o2(&c) != 10)
        return 2;
    overloaded<A, B, C> o3{A{}, B{}, C{}};
    if (o3(1) != 2 || o3(&c) != 10 || o3(2.5) != 102)
        return 3;
    auto l = overloaded{[](int x) { return x * 2; },
                        [](const char *s) { return s ? 5 : 6; },
                        [&](double d) { return static_cast<int>(d) + c; }};
    if (l(four) != 8 || l(text) != 5 || l(one_and_half) != 1)
        return 4;
    const auto &cl = l;
    if (cl(three) != 6)
        return 5;
    auto one = overloaded{[](long x) { return x + 1; }};
    if (one(long_one) != 2)
        return 6;
    int base = 7;
    if (apply(overloaded{[&](int x) { return x + base; },
                         [](double d) { return static_cast<int>(d * 2); }}) !=
        805)
        return 7;
    // objects, not lambda-expressions, are the bases' values
    auto f = [](int x) { return x + 1; };
    auto g = [](double d) { return static_cast<int>(d) * 2; };
    B b;
    auto o = overloaded{f, g, b};
    volatile int v = 1;
    volatile double dv = 2.5;
    if (o(v) + o(dv) + o(&c) != 2 + 4 + 10)
        return 8;
    int multiplier = 3;
    auto captured = overloaded{[&](int x) { return x + base; },
                               [&](double d) {
                                   return static_cast<int>(d) * multiplier;
                               }};
    if (captured(one_) != 8 || captured(dv) != 6)
        return 9;
    return 0;
}
