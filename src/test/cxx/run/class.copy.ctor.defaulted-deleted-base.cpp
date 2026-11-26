// [class.copy.ctor]/10.1: an explicitly defaulted copy constructor on a class
// whose base has a deleted copy constructor is defined as deleted (not
// ill-formed unless used), as in libc++ std::variant __move_constructor.

struct NonCopyable {
    int x;
    NonCopyable(int v) : x(v) {}
    NonCopyable(const NonCopyable &) = delete;
    NonCopyable(NonCopyable &&o) : x(o.x) { o.x = 0; }
};

struct MoveOnly : NonCopyable {
    using NonCopyable::NonCopyable;
    MoveOnly(const MoveOnly &) = default;
    MoveOnly(MoveOnly &&o) : NonCopyable(static_cast<NonCopyable &&>(o)) {}
};

int main()
{
    MoveOnly a(42);
    MoveOnly b(static_cast<MoveOnly &&>(a));
    if (b.x != 42 || a.x != 0)
        return 1;
    return 0;
}
