// [over.match.class.deduct]: class template argument deduction over lambdas
// and a using pack of operator().
template <typename... Ts> struct overload : Ts... {
    using Ts::operator()...;
};
template <typename... Ts> overload(Ts...) -> overload<Ts...>;

int main() {
    const auto a = overload{[](int) { return 1; }, [](double) { return 2; }};
    return a(0) + a(0.0) != 3;
}
