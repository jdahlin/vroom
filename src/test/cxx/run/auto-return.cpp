// function return type deduction: [dcl.spec.auto]/3, /8, /9, /11, and
// decltype(auto) for variables and return types
// ([dcl.type.auto.deduct]/5)
int store = 1;
int values[3] = {4, 5, 6};

auto sum(int a, int b) { return a + b; }
auto ratio(int a) { return a / 2.0; }
auto first(int *p) { return p; }
auto nothing() {}
auto early(int v) {
    if (v < 0)
        return;
    store = v;
}
auto sign(int v) {
    if (v < 0)
        return -1;
    if (v > 0)
        return 1;
    return 0;
}
/* once a return statement is seen the deduced type can be used */
auto factorial(int n) {
    if (n <= 1)
        return 1;
    return n * factorial(n - 1);
}
auto &element(int i) { return values[i]; }
const auto &read_only() { return store; }
auto decayed() { return values; }
decltype(auto) by_name() { return store; }
decltype(auto) by_expression() { return (store); }
decltype(auto) moved() { return static_cast<int &&>(store); }

struct Box {
    int value;
};
auto boxed(int v) { return Box{v}; }

int main() {
    int failures = 0;
    failures += sum(2, 3) != 5 || ratio(3) != 1.5;
    failures += *first(values) != 4;
    nothing();
    early(-1);
    failures += store != 1;
    early(9);
    failures += store != 9;
    failures += sign(-4) != -1 || sign(0) != 0 || sign(8) != 1;
    failures += factorial(5) != 120;
    element(1) = 50;
    failures += values[1] != 50;
    failures += read_only() != 9;
    failures += decayed()[2] != 6;
    int copy = by_name();
    copy++;
    failures += store != 9;
    by_expression() = 3;
    failures += store != 3;
    int &&r = moved();
    r = 4;
    failures += store != 4;
    failures += boxed(6).value != 6;
    int local = 2;
    decltype(auto) same = local;
    decltype(auto) ref = (local);
    ref = 5;
    same = 1;
    failures += local != 5 || same != 1;
    decltype(auto) result = sum(1, 1);
    failures += result != 2;
    return failures;
}
