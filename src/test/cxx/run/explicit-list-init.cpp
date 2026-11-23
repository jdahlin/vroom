/* [over.match.list]/1: an explicit constructor in direct-list-initialization
   and in T() (value-initialization, [dcl.init.general]/9); piecewise_construct's
   and __hash_combine's shapes */
extern "C" int printf(const char *, ...);

struct tag_t {
    explicit tag_t() = default;
};
inline constexpr tag_t tag = tag_t();

struct counter {
    int n;
    explicit counter() : n(7) {}
    explicit counter(int a, int b) : n(a * 10 + b) {}
};

struct zeroed {
    zeroed() = default;
    int a;
    long b = 5;
};

struct empty_base {};

template <class T> struct scalar_hash : public empty_base {
    unsigned long bias;
    unsigned long operator()(T v) const { return v.first + v.second + bias; }
};

class hidden {
    int a;

public:
    int get() const { return a; }
};

struct pair_t {
    unsigned long first, second;
};

static unsigned long combine(unsigned long lhs, unsigned long rhs)
{
    const pair_t p = {lhs, rhs};
    return scalar_hash<pair_t>()(p) * 31 + p.second;
}

static int use(tag_t) { return 1; }

int main()
{
    counter a{};
    counter b{3, 4};
    counter c = counter();
    tag_t t{};
    zeroed z = zeroed();
    zeroed w{};
    hidden h = hidden();
    printf("%d %ld %d %ld %d\n", z.a, z.b, w.a, w.b, h.get());
    printf("%d %d %d %d %d %lu\n", a.n, b.n, c.n, use(tag), use(t),
           combine(5, 6));
    return 0;
}
