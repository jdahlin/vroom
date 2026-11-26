// [except.spec]/8: noexcept of an implicitly-declared move constructor of a
// class with a move-only subobject (e.g. std::optional<FileHandle>).

struct NothrowMoveOnly {
    int x = 0;
    NothrowMoveOnly() = default;
    NothrowMoveOnly(const NothrowMoveOnly &) = delete;
    NothrowMoveOnly(NothrowMoveOnly &&o) noexcept : x(o.x) { o.x = 0; }
};

struct ThrowingMoveOnly {
    int x = 0;
    ThrowingMoveOnly() = default;
    ThrowingMoveOnly(const ThrowingMoveOnly &) = delete;
    ThrowingMoveOnly(ThrowingMoveOnly &&o) : x(o.x) { o.x = 0; }
};

struct WrapNothrow {
    NothrowMoveOnly m;
};

struct WrapThrowing {
    ThrowingMoveOnly m;
};

static_assert(__is_nothrow_constructible(WrapNothrow, WrapNothrow &&));
static_assert(!__is_nothrow_constructible(WrapThrowing, WrapThrowing &&));

int main() { return 0; }
