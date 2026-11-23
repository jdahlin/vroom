/* [dcl.init.list]/3.7, [over.match.list]/1.2: a braced list initializes a
   class with constructors through them, its elements the arguments */
struct K {
    int v;
    K(int a, int b) : v(a * 10 + b) {}
    K(long a) : v(static_cast<int>(a) + 100) {}
};
struct Holder {
    K k;
    int n;
};
K global{1, 2};
K from_long = {7L};
int sum(K k) { return k.v; }
int main()
{
    K local{3, 4};
    K copy = {5, 6};
    if (global.v != 12 || local.v != 34 || copy.v != 56)
        return 1;
    if (from_long.v != 107)
        return 2;
    Holder h = {{8, 9}, 1};
    if (h.k.v != 89 || h.n != 1)
        return 3;
    if (sum({2, 3}) != 23)
        return 4;
    return 0;
}
