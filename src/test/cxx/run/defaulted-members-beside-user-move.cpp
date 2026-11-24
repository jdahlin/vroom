// [class.copy.ctor]/6, /8, /10 and [class.copy.assign]/2: a class with a
// user-provided copy constructor, a defaulted move constructor and
// defaulted assignment operators over a base that has a deleted copy
// constructor or is not trivially copyable (libc++'s std::optional layering).

static int copies;
static int moves;

struct N {
    int x;
    N() : x(0) {}
    N(const N &o) : x(o.x) { copies++; }
};

struct Storage {
    union {
        char c;
        N n;
    };
    Storage() : c(0) {}
    ~Storage() {}
};

struct Layer : Storage {
    Layer() = default;
    Layer(const Layer &) {}
    Layer(Layer &&) = default;
    Layer &operator=(const Layer &) = default;
    Layer &operator=(Layer &&) = default;
};

struct Base {
    int v;
    Base() : v(0) {}
    Base(const Base &o) : v(o.v) { copies++; }
    ~Base() {}
};

struct Mid : Base {
    Mid() = default;
    Mid(const Mid &o) : Base(o) {}
    Mid(Mid &&) = default;
    Mid &operator=(const Mid &) = default;
    Mid &operator=(Mid &&) = default;
};

template <class T>
struct Top : Mid {
    T extra;
    Top() = default;
    Top(const Top &) = default;
    Top(Top &&o) : Mid(static_cast<Mid &&>(o)) { moves++; }
    Top &operator=(const Top &) = default;
    Top &operator=(Top &&) = default;
};

int main()
{
    Layer a;
    Layer b(a);
    Layer c(static_cast<Layer &&>(a));
    c = b;
    c = static_cast<Layer &&>(b);

    Top<int> t;
    t.v = 5;
    Top<int> u(t);
    if (u.v != 5)
        return 1;
    Top<int> w(static_cast<Top<int> &&>(t));
    if (moves != 1 || w.v != 5)
        return 2;
    Mid m;
    m.v = 7;
    Mid n;
    n = m;
    if (n.v != 7)
        return 3;
    n.v = 9;
    m = static_cast<Mid &&>(n);
    if (m.v != 9)
        return 4;
    return 0;
}
