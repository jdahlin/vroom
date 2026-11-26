// [class.copy.assign]: assignability and trivial-assignability traits for
// classes with non-trivial or move-only members.

struct MoveOnly {
    int x = 0;
    MoveOnly() = default;
    MoveOnly(const MoveOnly &) = delete;
    MoveOnly &operator=(const MoveOnly &) = delete;
    MoveOnly(MoveOnly &&o) noexcept : x(o.x) { o.x = 0; }
    MoveOnly &operator=(MoveOnly &&o) noexcept
    {
        x = o.x;
        o.x = 0;
        return *this;
    }
};

struct Wrapper {
    MoveOnly m;
};

static_assert(!__is_assignable(Wrapper &, const Wrapper &));
static_assert(!__is_trivially_assignable(Wrapper &, const Wrapper &));
static_assert(!__is_trivially_assignable(Wrapper &, Wrapper &&));

int main() { return 0; }
