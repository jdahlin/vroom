/* [ext:gnu_vector_size] a vector as a value: initialized and assigned from a
   vector of its type, passed, returned, a member copied with its class, read
   back through memcpy (operators and v[i] are not analysed yet). */
typedef int v4si __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));
typedef long long m64 __attribute__((vector_size(8)));

struct S {
    char tag;
    v4si i;
    v2df d;
    m64 m;
};

v4si global;

v4si pass(v4si a, v2df, m64, v4si c)
{
    v4si b = a;
    global = c;
    return b;
}

v2df swap_d(v2df a, v2df b)
{
    v2df t = a;
    a = b;
    b = t;
    return a;
}

int lane(v4si v, int i)
{
    int words[4];
    __builtin_memcpy(words, &v, sizeof v);
    return words[i];
}

int main()
{
    int init[4] = {1, 2, 3, 4}, other[4] = {10, 20, 30, 40};
    S s;
    s.tag = 'x';
    __builtin_memcpy(&s.i, init, 16);
    __builtin_memcpy(&global, other, 16);
    double dd[2] = {1.5, 2.5}, ee[2] = {3.5, 4.5};
    v2df x, y;
    __builtin_memcpy(&x, dd, 16);
    __builtin_memcpy(&y, ee, 16);
    S t = s;
    S u;
    u = t;
    v4si r = pass(u.i, u.d, u.m, global);
    v2df z = swap_d(x, y);
    double back[2];
    __builtin_memcpy(back, &z, 16);
    if (lane(r, 0) != 1 || lane(r, 3) != 4)
        return 1;
    if (lane(global, 1) != 20 || lane(u.i, 2) != 3)
        return 2;
    if (back[0] != 3.5 || back[1] != 4.5)
        return 3;
    return 0;
}
