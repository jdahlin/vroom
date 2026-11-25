/* [expr.const]/3: a range-for's const variable is not usable in constant
   expressions: a lambda that names it captures it */
int main()
{
    int numbers[] = {1, 2, 3};
    int r = 0;
    for (const auto i : numbers) {
        auto l = [&]() { r += i; };
        l();
        auto c = [=]() { return i; };
        r += c();
    }
    return r != 12;
}
