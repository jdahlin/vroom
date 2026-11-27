// An explicitly-declared explicit copy constructor defaulted on its first
// declaration copies as the implicit one ([class.copy.ctor]/14).
struct Hard {
    Hard(int v) : a(v), b(v + 1) {}
    Hard(Hard &&that) = default;
    explicit Hard(const Hard &) = default;
    int a, b;
};

struct Only {
    Only() = default;
    explicit Only(const Only &) = default;
    int a = 5;
};

struct Holder {
    Holder(const Hard &h) : hard(h) {}
    Hard hard;
};

static int sum(Hard h) { return h.a + h.b; }

int main()
{
    Hard x(1);
    Hard y(x);
    Holder h(y);
    Only o;
    Only p(o);
    return sum(Hard(y)) + sum(static_cast<Hard &&>(h.hard)) + p.a - 11;
}
