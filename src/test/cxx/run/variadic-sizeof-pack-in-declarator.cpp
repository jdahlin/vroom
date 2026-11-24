// [temp.variadic]/7, [expr.sizeof]/5: sizeof...(Is) in the declarator names the pack without being a pack expansion.
template <int... Is> struct seq {};
template <typename C, int... Is>
auto count(C c, seq<Is...>) -> char (&)[sizeof...(Is)];
template <typename C, int... Is>
auto count(C c, seq<Is...>) -> char (&)[sizeof...(Is)] { static char a[sizeof...(Is)]; return a; }
struct F {};
int main() {
    static_assert(sizeof(count(F{}, seq<1, 2, 3>{})) == 3, "");
    if (sizeof(count(F{}, seq<4, 5>{})) != 2)
        return 1;
    return 0;
}
