// Unrestricted union ([class.union]/4): anonymous union containing a class
// with a user-defined constructor, as in libc++'s __optional_destruct_base.

struct Point {
    int x;
    int y;
    constexpr Point(int a, int b) : x(a), y(b) {}
};

template <class T>
struct OptionalBase {
    union {
        char null_state;
        T val;
    };
    bool engaged;

    constexpr OptionalBase() noexcept : null_state(), engaged(false) {}
    constexpr OptionalBase(int a, int b) : val(a, b), engaged(true) {}
};

int main() {
    OptionalBase<Point> empty;
    OptionalBase<Point> full(7, 8);
    if (empty.engaged || !full.engaged)
        return 1;
    return (full.val.x == 7 && full.val.y == 8) ? 0 : 2;
}
