// [class.copy.ctor]: constructing a non-copyable class (deleted implicit copy
// constructor) from a string literal / array argument (e.g.
// `SpriteGroupPool _spritegroup_pool("SpriteGroup");`).

struct NonCopyable {
    NonCopyable() = default;
    NonCopyable(const NonCopyable &) = delete;
};

struct Pool {
    const char *name;
    NonCopyable guard;
    Pool(const char *n) : name(n) {}
};

int main()
{
    Pool p("SpriteGroup");
    if (p.name[0] != 'S')
        return 1;
    return 0;
}
