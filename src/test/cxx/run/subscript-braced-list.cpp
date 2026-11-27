/* [expr.sub], [over.sub]: a braced-init-list subscript is the operator[]'s
   argument */
extern "C" int printf(const char *, ...);
struct key {
    const char *f;
    int id;
};
struct map {
    int v[16] = {};
    int &operator[](const key &k) { return v[k.id]; }
    int at(int i) const { return v[i]; }
};
struct pr {
    int a, b;
    pr(int x, int y) : a(x), b(y) { printf("pr %d %d\n", a, b); }
    ~pr() { printf("~pr %d\n", a); }
};
struct m2 {
    int total = 0;
    void operator[](pr p) { total += p.a * 10 + p.b; }
};
template <class M> void fill(M &m, int i) { m[{"t", i}] = i * 3; }
int main()
{
    map m;
    m[{"a", 3}] = 7;
    int k = 4;
    m[{"b", k}] += 2;
    fill(m, 5);
    m2 q;
    q[{k, 2}];
    printf("%d %d %d %d\n", m.at(3), m.at(4), m.at(5), q.total);
}
