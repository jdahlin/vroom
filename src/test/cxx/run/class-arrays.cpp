/* [dcl.init]/7.2: each element of an array default-initialized, first to
   last; [class.dtor]/13: destroyed in the reverse order */
int order[16], events;
struct D {
    int id;
    D() : id(events) { order[events++] = id; }
    ~D() { order[events++] = 100 + id; }
};
struct Plain {
    int v;
    Plain() : v(7) {}
};
int sum()
{
    Plain p[4];
    int total = 0;
    for (int i = 0; i < 4; i++)
        total += p[i].v;
    return total;
}
int main()
{
    {
        D d[3];
        if (events != 3 || d[0].id != 0 || d[2].id != 2)
            return 1;
    }
    if (events != 6)
        return 2;
    static const int expected[6] = {0, 1, 2, 102, 101, 100};
    for (int i = 0; i < 6; i++)
        if (order[i] != expected[i])
            return 3 + i;
    if (sum() != 28)
        return 10;
    return 0;
}
