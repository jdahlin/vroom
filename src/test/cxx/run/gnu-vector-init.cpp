/* [ext:gnu_vector_size] a vector list-initialized: the elements in order,
   the rest zero; vectors as elements of arrays and members. */
typedef int v4si __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));
typedef unsigned char v8qu __attribute__((vector_size(8)));

struct S {
    int tag;
    v4si v;
};

v4si global = {7, 8, 9, 10};
v2df gd = {1.5};

int main()
{
    v4si a = {1, 2, 3, 4};
    v4si b = {5, 6};
    v4si z = {};
    v2df d = {2.5, -1.0};
    v8qu q = {1, 2, 3, 4, 5, 6, 7, 8};
    v4si arr[2] = {{1, 2, 3, 4}, {5}};
    S s = {9, {10, 20, 30, 40}};
    int n = 3;
    v4si dyn = {n, n + 1, n * 2, -n};
    if (a[3] != 4 || b[1] != 6 || b[2] != 0 || b[3] != 0 || z[2] != 0)
        return 1;
    if (d[0] != 2.5 || d[1] != -1.0 || gd[0] != 1.5 || gd[1] != 0.0)
        return 2;
    if (q[7] != 8 || arr[0][2] != 3 || arr[1][0] != 5 || arr[1][3] != 0)
        return 3;
    if (s.tag != 9 || s.v[3] != 40 || global[0] != 7 || global[3] != 10)
        return 4;
    if (dyn[0] != 3 || dyn[1] != 4 || dyn[2] != 6 || dyn[3] != -3)
        return 5;
    return 0;
}
