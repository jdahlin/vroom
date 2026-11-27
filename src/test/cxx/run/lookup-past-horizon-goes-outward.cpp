// [temp.dep.candidate]/1: a namespace's functions declared after a template
// are not found from its definition, whose lookup goes on outward;
// argument-dependent lookup finds those of an associated namespace.
struct Rect {
    int w;
};
static int f(long) { return 1; }
static int operator&(const Rect &, int) { return 10; }
static const Rect *operator&(const Rect &, long) { return nullptr; }

namespace n
{
struct S {
    int v;
};
template <typename T> int call(T t)
{
    Rect r{t};
    const Rect *p = &r;
    return f(t) + (r & 0) + p->w;
}
template <typename T> int adl(T t) { return g(t) + f(0); }
inline int f(int) { return 100; }
inline int operator&(const Rect &, int) { return 1000; }
inline int g(S s) { return s.v; }
} // namespace n

int main() { return n::call(3) + n::adl(n::S{5}) - 20; }
