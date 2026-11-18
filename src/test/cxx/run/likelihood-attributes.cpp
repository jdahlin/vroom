// [dcl.attr.likelihood]: likely and unlikely on statements (Skia's
// SK_LIKELY / SK_UNLIKELY): hints, the statements' meaning unchanged
int f(bool e)
{
    if (e) [[unlikely]] {
        return 1;
    }
    if (!e) [[likely]]
        return 2;
    return 0;
}
int g(int n)
{
    int s = 0;
    while (n > 0) [[likely]] {
        s += n;
        n--;
    }
    for (int i = 0; i < 3; i++) [[unlikely]] [[unlikely]] s++;
    [[likely]] return s;
}
int main() { return f(false) == 2 && f(true) == 1 && g(3) == 9 ? 0 : 1; }
