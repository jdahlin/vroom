/* [basic.lookup.argdep]/4.2: "All names except those of (possibly
   overloaded) functions and function templates are ignored": a class,
   alias or variable template of the called name in an associated namespace
   neither stops the call nor joins it. */

namespace lib {
template <class T> struct hash {};
template <class T> using name = T;
template <class T> constexpr int value = 1;
struct arg {};
} // namespace lib

namespace app {
template <class T> struct box {
    int depth;
};

template <class T> int hash(const box<T> &b)
{
    if (b.depth == 0)
        return 1;
    box<T> inner = {b.depth - 1};
    return 2 * hash(inner);
}

int name(const lib::arg &) { return 3; }

template <class T> int value(const T &) { return 5; }

int run()
{
    box<lib::arg> b = {3};
    lib::arg a;
    return hash(b) + name(a) + value(a);
}
} // namespace app

int main() { return app::run() == 16 ? 0 : 1; }
