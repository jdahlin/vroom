// [class.copy.ctor]: a deleted copy constructor taking a non-const lvalue
// reference (`X(X &) = delete;`, e.g. Catch2's `ScopedMessage`).

struct ScopedMessage {
    int val;
    explicit ScopedMessage(int v) : val(v) {}
    ScopedMessage(ScopedMessage &) = delete;
    ScopedMessage(ScopedMessage &&o) : val(o.val) { o.val = 0; }
};

static_assert(!__is_constructible(ScopedMessage, ScopedMessage &));
static_assert(__is_constructible(ScopedMessage, ScopedMessage &&));

int main()
{
    ScopedMessage a(9);
    ScopedMessage b(static_cast<ScopedMessage &&>(a));
    if (a.val != 0 || b.val != 9)
        return 1;
    return 0;
}
