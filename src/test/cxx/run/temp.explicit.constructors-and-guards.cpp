// [temp.explicit]/10: an explicit instantiation definition emits the declared
// constructors too; [temp.inst]/4: an out-of-class member of a pack that is
// empty or longer than one, its parameter named; an inline function's local
// static has a guard.
template <class A> struct Net {
    Net(int);
    Net(const Net &o);
    A a;
};
template <class A> Net<A>::Net(int v) : a(v) {}
template <class A> Net<A>::Net(const Net<A> &o) : a(o.a) {}
template class Net<int>;
template <class> struct fun;
template <class R, class... A> struct fun<R(A...)> {
    int v;
    R operator()(A...) const;
};
template <class R, class... A> R fun<R(A...)>::operator()(A... a) const {
    return R(v + (0 + ... + a));
}
struct T {
    int v;
    T() : v(5) {}
};
inline T &instance() {
    static T t;
    return t;
}
int main() {
    Net<int> n(3), m(n);
    fun<int()> z{1};
    fun<int(int, int)> two{2};
    return m.a != 3 || z() != 1 || two(3, 4) != 9 || instance().v != 5;
}
