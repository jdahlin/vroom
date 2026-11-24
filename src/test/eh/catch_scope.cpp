// The C++ half of catch_scope.c (bench/vcc-eh.py builds it with clang++).
extern "C" {
void cxx_throw(int k);
int basic(int k);
int only_int(int k);
int nested(int k);
int rethrow(int k);
extern int s_order[32], s_n;
}

void cxx_throw(int k)
{
    if (k == 1)
        throw 7;
    if (k == 2)
        throw 8L;
    if (k == 3)
        throw "text";
}

static bool order_is(const int *want, int n)
{
    bool ok = s_n == n;

    for (int i = 0; i < n && ok; i++)
        ok = s_order[i] == want[i];
    s_n = 0;
    return ok;
}

#define ORDER_IS(...)                                                          \
    ([] {                                                                      \
        static const int w[] = {__VA_ARGS__};                                  \
        return order_is(w, sizeof w / sizeof w[0]);                            \
    }())

int main()
{
    if (basic(0) != 100 || !ORDER_IS(1))
        return 1;
    if (basic(1) != 17 || !ORDER_IS(1, 90))
        return 2;
    if (basic(2) != 20 || !ORDER_IS(1, 90))
        return 3;
    if (basic(3) != 20 || !ORDER_IS(1, 90))
        return 4;
    if (only_int(0) != 100 || !ORDER_IS(3, 2))
        return 5;
    if (only_int(1) != 17 || !ORDER_IS(3, 90, 2))
        return 6;
    try {
        only_int(2);
        return 7;
    } catch (long k) {
        if (k != 8 || !ORDER_IS(3, 2))
            return 8;
    }
    try {
        only_int(3);
        return 9;
    } catch (const char *) {
        if (!ORDER_IS(3, 2))
            return 10;
    }
    if (nested(0) != 100 || !ORDER_IS(5, 4))
        return 11;
    if (nested(1) != 17 || !ORDER_IS(5, 90, 4))
        return 12;
    if (nested(2) != 28 || !ORDER_IS(5, 4, 90))
        return 13;
    try {
        nested(3);
        return 14;
    } catch (const char *) {
        if (!ORDER_IS(5, 4))
            return 15;
    }
    if (rethrow(0) != 100)
        return 16;
    if (rethrow(1) != 1025 || !ORDER_IS(90, 90))
        return 17;
    return 0;
}
