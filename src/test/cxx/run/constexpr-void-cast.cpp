// [expr.static.cast]/6: a conversion to void in a constexpr function.
constexpr int check(int x) {
    (void)(x == 0);
    static_cast<void>(0);
    return x + 1;
}
static_assert(check(4) == 5, "");
int main() {
    return check(9) != 10;
}
