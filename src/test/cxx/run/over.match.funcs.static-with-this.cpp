// [over.match.funcs]/4: calling a static member function overload from inside
// a non-static member function of the same class (where `this` is bound as the
// implied object argument, e.g. `SpriteGroup::Resolve(child, ctx)`).

struct Node {
    const Node *child;
    int val;
    Node(const Node *c, int v) : child(c), val(v) {}
    virtual ~Node() = default;
    virtual int Resolve(int x) const
    {
        if (child)
            return Node::Resolve(child, x + val);
        return x + val;
    }
    static int Resolve(const Node *n, int x)
    {
        if (!n)
            return x;
        return n->Resolve(x);
    }
};

int main()
{
    Node leaf{nullptr, 30};
    Node root{&leaf, 10};
    if (root.Resolve(2) != 42)
        return 1;
    return 0;
}
