// [class.base.init]/9.1: a member of class type with constructors, initialized
// from its default member initializer: copy-initialization by a converting
// constructor (ast::node's `node node = 0`), direct and list forms.
extern "C" int printf(const char *, ...);

static int calls;

struct Node {
    unsigned i = 0;
    constexpr Node() = default;
    constexpr Node(decltype(nullptr)) {}
    explicit constexpr Node(unsigned i) : i(i) { calls++; }
    Node(int a, int b) : i(a + b) { calls++; }
};

struct Facts {
    Node node = 0;
    Node explicit_node{7u};
    Node pair{3, 4};
    Node paren = Node(9u);
    int count = 5;
};

struct Own {
    Node n = 0;
    Own() {}
    Own(int) : n(1u) {}
};

int main()
{
    Facts f;
    Own a, b(1);
    printf("%u %u %u %u %d %u %u %d\n", f.node.i, f.explicit_node.i, f.pair.i,
           f.paren.i, f.count, a.n.i, b.n.i, calls);
    return 0;
}
