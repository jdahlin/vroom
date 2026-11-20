/* [ext:gnu_vector_size] v[i]: an lvalue lane of a vector object (read,
   written, compound-assigned), a prvalue lane of a vector rvalue. */
typedef int v4si __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));
typedef unsigned char v16qu __attribute__((vector_size(16)));

v4si g;

v4si make(int a, int b, int c, int d)
{
    v4si v;
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
    return v;
}

int sum(const v4si &v) { return v[0] + v[1] + v[2] + v[3]; }

int main()
{
    v4si a = make(1, 2, 3, 4);
    a[1] += 40;
    a[2]++;
    --a[3];
    int i = 2;
    if (a[0] != 1 || a[1] != 42 || a[i] != 4 || a[3] != 3)
        return 1;
    if (make(5, 6, 7, 8)[2] != 7 || (a + a)[1] != 84)
        return 2;
    v2df d;
    d[1] = 2.5;
    d[0] = d[1] * 2;
    if (d[0] != 5.0 || d[1] != 2.5)
        return 3;
    g = a;
    g[3] = 100;
    if (sum(g) != 1 + 42 + 4 + 100 || sum(a) != 1 + 42 + 4 + 3)
        return 4;
    return 0;
}
