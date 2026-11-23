/* [dcl.init.list]/3.9, /3.10: a reference list-initialized (a function's
   parameter, a declaration): the prvalue of the referenced type is bound */
extern "C" int printf(const char *, ...);
struct rec { int a; long b; };
struct holder {
    rec items[4];
    int n = 0;
    void append(const rec &r) { items[n++] = r; }
    void move_append(rec &&r) { items[n++] = r; }
};
struct two { int x; int y; };
static int take(const two &t) { return t.x + t.y; }
static int take2(two &&t) { return t.x * t.y; }
static long widen(const long &v) { return v + 1; }
static int through(const int &v) { return v * 2; }
struct sum_of { sum_of(int a, int b) : v(a + b) {} int v; };
struct derived : two { };
static int use(const sum_of &s) { return s.v; }
static int bases(derived &d)
{
    /* (3.9): reference-related, bound to the object itself */
    const two &alias = {d};
    d.x += 100;
    return alias.x;
}
int g(int p, int q)
{
    holder h;
    h.append({p, q});
    h.move_append({q, 1});
    const two &r = {p, q};
    two &&s = {q, p};
    const long &w = {p};
    int one = 5;
    const int &same = {one};
    int sum = h.items[0].a + (int)h.items[1].b + take({p, q}) + take2({2, q});
    sum += r.x + s.y + (int)w + same + (int)widen({3}) + through({p});
    const sum_of &made = {1, 2};
    derived d;
    d.x = 1;
    d.y = 2;
    sum += made.v + use({5, 6}) + bases(d) - 100 - 1;
    return sum;
}
int main()
{
    printf("%d\n", g(3, 4));
    return g(3, 4) - 43 - 3 - 11;
}
