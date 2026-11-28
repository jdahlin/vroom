// [class.temporary]: an aggregate temporary bound to a reference parameter in
// a namespace-scope initializer lives to the end of the full-expression
struct A {
    int x, y;
};
struct D {
    int v;
    D(const A &a) : v(a.x + a.y) {}
};
int f(const A &a) { return a.x - a.y; }
D d(A{1, 2});
D list[] = {D(A{3, 4}), D(A{5, 6})};
int e = f(A{9, 2});
int main()
{
    if (d.v != 3 || list[0].v != 7 || list[1].v != 11 || e != 7)
        return 1;
    return 0;
}
