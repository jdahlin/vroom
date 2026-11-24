// [class.mem]/9, [class.union.anon]/1: default member initializers of an
// unnamed struct, an anonymous union and an unnamed class used by a typedef.
struct Outer {
    union {
        int i = 3;
        float f;
    };
    struct {
        int a = 4;
        int b = 5;
    } pair;
};
typedef struct { int v = 7; } Named;
static struct { int w = 9; } object;

int main() {
    Outer o;
    Named n;
    return o.i != 3 || o.pair.a != 4 || o.pair.b != 5 || n.v != 7 || object.w != 9;
}
