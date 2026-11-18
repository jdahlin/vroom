/* [expr.ref]/6.1: a member of reference type is the object it refers to;
   [abi:class-types]: laid out as a pointer */
struct Counter {
    int &total;
    const int step;
    long tag;
    Counter(int &t, int s) : total(t), step(s), tag(7) {}
    void bump() { total += step; }
    int read() const { return total; }
};
struct Pair {
    int &other;
    int step;
};
int add(Counter c) { c.bump(); return c.total; }
int main()
{
    int sum = 1, extra = 40;
    Counter a(sum, 2);
    a.bump();
    if (sum != 3 || a.read() != 3)
        return 1;
    Counter b(a);
    b.bump();
    if (sum != 5 || &b.total != &sum)
        return 2;
    if (add(a) != 7 || sum != 7)
        return 3;
    Pair p = {extra, 2};
    p.other += p.step;
    Pair q = p;
    q.other++;
    if (extra != 43 || &q.other != &extra)
        return 4;
    if (sizeof(Counter) != 24 || sizeof(Pair) != 16)
        return 5;
    return 0;
}
