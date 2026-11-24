// A static const variable whose initializer passes its own address to a
// constexpr function along with a call that is not constant: dynamic
// initialization, evaluated at run time ([expr.const]/11, [basic.start.static]).
static int calls;
struct TS {
    int n;
    TS &operator*(const char *) { n = 7; return *this; }
};
static int set(const TS &t) { calls++; return t.n + 1; }
static constexpr int consume(const int *, int v) noexcept { return v; }
static const int registered = consume(&registered, set(TS() * ""));
int main()
{
    if (registered != 8)
        return 1;
    if (calls != 1)
        return 2;
    return 0;
}
