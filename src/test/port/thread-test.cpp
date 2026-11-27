/* port/thread.h with no C++ library:
   clang -x c++ -std=gnu++20 -fno-exceptions -fno-rtti -nostdinc++ -Isrc/include
   src/test/port/thread-test.cpp -o t -pthread   (the C driver links) */
#include <stdio.h>

#include "port/thread.h"

static int counted_destructions;
struct move_only {
    int v;
    bool owner = true;
    explicit move_only(int v) : v(v) {}
    move_only(move_only &&o) noexcept : v(o.v) { o.owner = false; }
    move_only(const move_only &) = delete;
    ~move_only()
    {
        if (owner)
            __atomic_add_fetch(&counted_destructions, 1, __ATOMIC_RELAXED);
    }
};

static void add(int *to, int n) { __atomic_add_fetch(to, n, __ATOMIC_RELAXED); }

int main()
{
    int sum = 0, bad = 0;
    port::thread t[8];
    for (int i = 0; i < 8; i++)
        t[i] = port::thread(add, &sum, i);
    for (port::thread &x : t)
        x.join();
    bad |= sum != 28;
    {
        port::thread m([&sum](move_only a) { sum += a.v; }, move_only(14));
        port::thread n(static_cast<port::thread &&>(m));
        bad |= m.joinable() || !n.joinable();
        n.join();
        bad |= n.joinable();
    }
    bad |= sum != 42 || counted_destructions != 1;
    bad |= port::thread::hardware_concurrency() == 0;
    if (bad)
        printf("FAIL: sum %d, destructions %d\n", sum, counted_destructions);
    else
        printf("ok\n");
    return bad;
}
