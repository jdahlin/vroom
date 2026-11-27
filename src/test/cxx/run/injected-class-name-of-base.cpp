// [class.pre]/2, [class.member.lookup]: a base's injected-class-name is
// found in the derived class, before the namespace's names.
namespace n
{
struct GM {
    int g = 3;
    virtual ~GM() {}
};
struct Mid : GM {
};
} // namespace n

struct GM {
    long other[4];
};

struct B : n::Mid {
    using INHERITED = GM;
    int get(const INHERITED &x) { return x.g; }
    GM *self() { return this; }
};

int main()
{
    B b;
    return b.get(b) + b.self()->g + B::GM().g - 9;
}
