/* [class.union.anon]: anonymous union members, their names bound in the
   enclosing class, nested ones, and clang's anonymous structs */
struct V {
    int kind;
    union {
        int i;
        float f;
        struct {
            short lo, hi;
        };
    };
    int get() const { return i; }
    void set(int v) { i = v; }
};

union W {
    char c;
    union {
        int a;
        long b;
    };
};

int main()
{
    V v;
    v.kind = 1;
    v.set(0x00020001);
    int bad = 0;
    if (v.get() != 0x00020001) bad |= 1;
    if (v.lo != 1 || v.hi != 2) bad |= 2;
    v.f = 1.0f;
    if (v.i != 0x3f800000) bad |= 4;
    W w;
    w.b = 0;
    w.a = 7;
    if (w.c != 7) bad |= 8;
    if (sizeof(V) != 8 || sizeof(W) != 8) bad |= 16;
    return bad;
}
