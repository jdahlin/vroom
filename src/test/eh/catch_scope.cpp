#include <initializer_list>
#include <cstddef>
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

static bool order_is(std::initializer_list<int> want)
{
    bool ok = static_cast<size_t>(s_n) == want.size();
    int i = 0;
    for (int w : want)
        ok = ok && i < s_n && s_order[i++] == w;
    s_n = 0;
    return ok;
}

int main()
{
    if (basic(0) != 100 || !order_is({1}))
        return 1;
    if (basic(1) != 17 || !order_is({1, 90}))
        return 2;
    if (basic(2) != 20 || !order_is({1, 90}))
        return 3;
    if (basic(3) != 20 || !order_is({1, 90}))
        return 4;
    if (only_int(0) != 100 || !order_is({3, 2}))
        return 5;
    if (only_int(1) != 17 || !order_is({3, 90, 2}))
        return 6;
    try {
        only_int(2);
        return 7;
    } catch (long k) {
        if (k != 8 || !order_is({3, 2}))
            return 8;
    }
    try {
        only_int(3);
        return 9;
    } catch (const char *) {
        if (!order_is({3, 2}))
            return 10;
    }
    if (nested(0) != 100 || !order_is({5, 4}))
        return 11;
    if (nested(1) != 17 || !order_is({5, 90, 4}))
        return 12;
    if (nested(2) != 28 || !order_is({5, 4, 90}))
        return 13;
    try {
        nested(3);
        return 14;
    } catch (const char *) {
        if (!order_is({5, 4}))
            return 15;
    }
    if (rethrow(0) != 100)
        return 16;
    if (rethrow(1) != 1025 || !order_is({90, 90}))
        return 17;
    return 0;
}
