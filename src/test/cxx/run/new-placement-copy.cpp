// [expr.new]/19, [dcl.init]/17.6.2: a placement new-expression initializing
// the object from an lvalue of its class (the copy of std::construct_at).
typedef decltype(sizeof 0) size_t;
inline void *operator new(size_t, void *p) noexcept { return p; }

struct Options {
    int a;
    int b;
    Options() : a(0), b(0) {}
    Options(const Options &) = default;
    Options &operator=(const Options &that)
    {
        this->~Options();
        new (this) Options(that);
        return *this;
    }
};

struct Plain {
    int x;
    long y;
    Plain(int x_, long y_) : x(x_), y(y_) {}
    Plain(const Plain &) = default;
};

int main()
{
    Options p;
    Options q;
    q.a = 3;
    q.b = 4;
    p = q;
    if (p.a != 3 || p.b != 4)
        return 1;
    alignas(Plain) char buffer[sizeof(Plain)];
    Plain source(7, 9);
    Plain *copy = new (buffer) Plain(source);
    if (copy->x != 7 || copy->y != 9)
        return 2;
    return 0;
}
