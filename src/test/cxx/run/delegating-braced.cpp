/* [class.base.init]/6: a delegating constructor with a braced-init-list */
struct Style {
    int v;
    constexpr Style(int a, int b) : v(a * 10 + b) {}
    constexpr Style() : Style{4, 2} {}
};
int main()
{
    Style s;
    static constexpr Style c;
    return s.v != 42 || c.v != 42;
}
