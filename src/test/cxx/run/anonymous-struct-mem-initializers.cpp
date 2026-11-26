/* [class.base.init]/9, /13.3: a mem-initializer that names one member of an
   anonymous struct (libc++'s compressed pairs) leaves the other members
   their default initialization, in declaration order; it was skipped, and a
   copied empty std::map kept its root pointer uninitialized */
extern "C" int printf(const char *, ...);
void *operator new(decltype(sizeof 0), void *p) noexcept { return p; }

static int order[16], steps;
static int record(int v)
{
    order[steps++] = v;
    return v;
}

struct Node {
    void *left;
    Node() : left() { record(1); }
};
struct Alloc {
};

struct Tree {
    void *begin;
    struct {
        Node end;
        Alloc alloc;
        int dflt = record(2);
        int named;
        int last = record(4);
    };
    struct {
        union {
            int u;
            float f;
        };
        Node after;
    };
    Tree() : named(record(3)) {}
    Tree(const Tree &) : alloc(), named(record(3)), f(1.5f) {}
};

static bool check(const Tree &t, const char *what)
{
    static const int expected[] = {1, 2, 3, 4, 1};
    bool ok = steps == 5 && !t.end.left && !t.after.left && t.dflt == 2 &&
              t.named == 3 && t.last == 4;
    for (int i = 0; ok && i < 5; i++)
        ok = order[i] == expected[i];
    if (!ok)
        printf("%s: %d steps\n", what, steps);
    return ok;
}

alignas(Tree) static unsigned char storage[2][sizeof(Tree)];

int main()
{
    for (auto &buffer : storage)
        for (unsigned char &byte : buffer)
            byte = 0xbe;
    Tree *a = new (storage[0]) Tree;
    if (!check(*a, "default"))
        return 1;
    steps = 0;
    Tree *b = new (storage[1]) Tree(*a);
    if (!check(*b, "copy") || b->f != 1.5f)
        return 2;
    return 0;
}
