/* Branch cleanup must preserve destinations, effects, and loop back edges. */
#define NI __attribute__((noinline))
static volatile int ft_seen;
NI int ft_adjacent(int x)
{
    goto first;
first:
second:
third:
    return x + 9;
}
NI int ft_forward(int x)
{
    if (x) goto first;
    goto second;
first:
second:
third:
    ft_seen += 1;
    return x + 7;
}
NI int ft_condition(int x)
{
    if (x < 0) { ft_seen += 3; return -x; }
    ft_seen += 5;
    return x + 2;
}
NI int ft_backward(int n)
{
    int sum = 0;
again:
    sum += n;
    if (--n > 0) goto again;
    return sum;
}
NI int ft_loops(int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++) {
        if (i == 2) continue;
        if (i == 5) break;
        sum += i;
    }
    while (n > 0) {
        n--;
        if (n & 1) continue;
        sum += n;
    }
    do {
        sum += 3;
        if (--n == -1) continue;
        sum += 20;
    } while (n > -2);
    return sum;
}
NI int ft_switch(int n)
{
    int x = 1;
    switch (n) {
    case 0: x += 2;
    case 1: x += 4; break;
    case 2: goto done;
    default: x += 8;
    }
done:
    return x;
}
/* Inline asm is an opaque record even when its emitted text is empty. */
NI int ft_asm(int x)
{
    if (x) goto target;
    __asm__ volatile ("" : : : "memory");
target:
    ft_seen += 7;
    return x;
}
int main(void)
{
    if (ft_adjacent(4) != 13) return 7;
    if (ft_forward(0) != 7 || ft_forward(3) != 10 || ft_seen != 2) return 1;
    if (ft_condition(-4) != 4 || ft_condition(4) != 6 || ft_seen != 10) return 2;
    if (ft_backward(1) != 1 || ft_backward(5) != 15) return 3;
    if (ft_loops(7) != 46 || ft_loops(0) != 26) return 4;
    if (ft_switch(0) != 7 || ft_switch(1) != 5 || ft_switch(2) != 1 || ft_switch(3) != 9) return 5;
    if (ft_asm(0) != 0 || ft_asm(3) != 3 || ft_seen != 24) return 6;
    return 0;
}
