// A nested function declarator after the outer parameter list grows.
int wide(int, int, int, int, int, int, int, int, const int value,
         int (*callback)(int, int), int last = 3)
{
    static_assert(__is_same(decltype(value), const int), "");
    return callback(value, last);
}
int call_wide(int (*add)(int, int))
{
    return wide(0, 1, 2, 3, 4, 5, 6, 7, 8, add);
}
