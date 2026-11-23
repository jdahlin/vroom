// The C++ half of catch_through_c.c (bench/vcc-eh.py builds it with clang++).
extern "C" {
void cxx_throw(int k);
int c_layer(int k);
int c_cleanup_throws(void);
extern int c_order[16], c_n;
}

void cxx_throw(int k) { throw k; }

static bool order_is(const int *want, int n)
{
    if (c_n != n)
        return false;
    for (int i = 0; i < n; i++)
        if (c_order[i] != want[i])
            return false;
    return true;
}

int main()
{
    static const int normal[] = {20, 21, 10, 30};
    static const int thrown[] = {20, 21, 10, 30};
    static const int cleanup[] = {41, 40};

    if (c_layer(0) != 7 || !order_is(normal, 4))
        return 1;
    c_n = 0;
    try {
        c_layer(1);
        return 2;
    } catch (int k) {
        if (k != 1 || !order_is(thrown, 4))
            return 3;
    }
    c_n = 0;
    try {
        c_cleanup_throws();
        return 4;
    } catch (int k) {
        if (k != 41 || !order_is(cleanup, 2))
            return 5;
    }
    return 0;
}
