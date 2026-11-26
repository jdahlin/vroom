// [class.static.mfct]/1, [expr.prim.id]: calling a static member function
// through a qualified-id when the overload set also contains a non-static
// member function (e.g. OpenTTD's `SpriteGroup::Resolve(group, *this)`).

struct Sprite {
    virtual ~Sprite() = default;
    virtual int Resolve(int x) const = 0;
    static int Resolve(const Sprite *s, int x)
    {
        if (s)
            return s->Resolve(x);
        return x + 1;
    }
};

struct Concrete : Sprite {
    int Resolve(int x) const override { return x * 2; }
};

struct Resolver {
    const Sprite *root;
    int DoResolve(int x) { return Sprite::Resolve(root, x); }
};

int main()
{
    Resolver r1{nullptr};
    if (r1.DoResolve(41) != 42)
        return 1;
    Concrete c;
    Resolver r2{&c};
    if (r2.DoResolve(21) != 42)
        return 2;
    return 0;
}
