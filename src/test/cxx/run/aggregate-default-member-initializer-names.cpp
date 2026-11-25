// An aggregate's default member initializer that names something other
// than a member (a static member function of another class, a static data
// member, an enumerator) and uses no `this`: an aggregate initialized from
// {} in a function body, in a default member initializer of another class
// and in a class template's (nlohmann::json's internal_iterator).
extern "C" int printf(const char *, ...);
struct lim { static constexpr long min() { return -5; } };
enum colour { red = 3, green };
class P {
  public:
    static constexpr long base = 10;
    long m_it = (lim::min)();
    long b = base + 1;
    colour c = green;
    long get() const { return m_it + b + c; }
};
template <typename T> struct I {
    T t {};
    P p {};
};
struct Q { P p {}; };
int main() {
    I<int> i;
    Q q;
    P l = {};
    printf("%ld %ld %ld %d\n", i.p.get(), q.p.get(), l.get(), i.t);
    return (int)(i.p.get() + q.p.get() + l.get() - 30);
}
