// [temp.deduct.guide], [dcl.meaning]/1: deduction guides and template
// declarations with operator/literal declarators do not poison unrelated
// parameter type names.
template <class T> struct Box {
    T v;
    Box(T x) : v(x) {}
};
template <class T> Box(T) -> Box<T>;

struct Tag { int n; };
template <class T> T add(T a, T b) { return a + b; }

int main() {
    Tag t{9};
    return add(t.n, 1) != 10;
}
