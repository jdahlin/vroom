// local and nested classes, and class names in functions and classes:
// [class.local], [class.nest], [basic.lookup.elab], [dcl.typedef]/3
struct G { int a; };
struct Outer {
    struct Inner { int x, y; };
    Inner in;
    int z;
};
typedef struct T { int q; } T;
typedef enum E { E0, E1 } E;

int sum(struct G *g) { return g->a; }

int main() {
    struct L { int a; char b; } l = {1, 2};
    struct G g;
    g.a = 3;
    struct G *pg = &g;
    Outer o;
    o.in.x = 4;
    o.in.y = 5;
    o.z = 6;
    Outer::Inner i = {7, 8};
    T t;
    t.q = 9;
    E e = E1;
    int failures = 0;
    failures += l.a + l.b != 3;
    failures += sum(pg) != 3;
    failures += o.in.x + o.in.y + o.z != 15;
    failures += i.x + i.y != 15;
    failures += t.q != 9 || e != 1;
    failures += sizeof(Outer) != 3 * sizeof(int);
    {
        struct L { long wide; } shadow = {10};
        failures += shadow.wide != 10 || sizeof shadow != sizeof(long);
    }
    struct Node { int value; struct Node *next; };
    Node second = {2, 0};
    Node first = {1, &second};
    failures += first.next->value != 2;
    return failures;
}
