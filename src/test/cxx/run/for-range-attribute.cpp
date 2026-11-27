// [stmt.ranged]: standard attributes on a for-range-declaration.
int main()
{
    int a[3] = {1, 2, 3};
    int n = 0, sum = 0;
    for ([[maybe_unused]] auto &x : a)
        n++;
    for ([[maybe_unused]] const int y : a)
        sum += y;
    return n * 10 + sum - 36;
}
