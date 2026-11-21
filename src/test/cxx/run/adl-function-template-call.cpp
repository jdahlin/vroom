// [basic.lookup.argdep]/1: an unqualified call to a function template
// includes function templates found by argument-dependent lookup.
template <class T> int shift(T &, int n) { return n; }

namespace custom {
template <class T> struct Wrap {
    T v;
};
template <class T> int shift(Wrap<T> &w, int n, int extra) {
    return w.v + n + extra;
}
}

int main() {
    custom::Wrap<int> x{1};
    return shift(x, 2, 3) != 6;
}
