/* [ext:clang_ext_vector_type] a scalar splats into an ext_vector_type;
   vectors of one size initialize each other by their bytes */
typedef float F __attribute__((ext_vector_type(4)));
typedef int I __attribute__((ext_vector_type(4)));
typedef float G __attribute__((vector_size(16)));
typedef long long L __attribute__((vector_size(16)));
extern constexpr F half = 0.5f;
extern const F big = 0x7f800000;
static int calls;
static int next() { calls++; return 3; }
static F from_gnu(G g) { return g; }
static G to_gnu(F f) { return f; }
static I from_other(L l) { return l; }
static bool all(F v, float x) { return v[0] == x && v[1] == x && v[2] == x && v[3] == x; }
int main()
{
    if (!all(half, 0.5f) || !all(big, 2139095040.0f))
        return 1;
    F f = next();
    if (calls != 1 || !all(f, 3.0f))
        return 2;
    double d = 2.5;
    F g = d;
    F c = (F)d;
    F s = static_cast<F>(7);
    F p = F(next());
    if (!all(g, 2.5f) || !all(c, 2.5f) || !all(s, 7.0f) || !all(p, 3.0f))
        return 3;
    G braced = G{1.0f};
    if (braced[0] != 1.0f || braced[1] != 0.0f || braced[3] != 0.0f)
        return 4;
    F back = from_gnu(to_gnu(f));
    if (!all(back, 3.0f))
        return 5;
    L l = {0x200000001LL, 0x400000003LL};
    I i = from_other(l);
    if (i[0] != 1 || i[1] != 2 || i[2] != 3 || i[3] != 4)
        return 6;
    G h = i;
    I j = h;
    if (j[0] != 1 || j[3] != 4)
        return 7;
    F e = F{};
    if (!all(e, 0.0f))
        return 8;
    return 0;
}
