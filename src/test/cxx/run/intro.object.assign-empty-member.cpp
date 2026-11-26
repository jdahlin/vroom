// [intro.object]/8: an empty member or base may share its address with another
// member, so copying one stores nothing (unique_ptr with an empty deleter).
/* cxx-run-flags: -std=c++20 */
template <class T> struct Del { void operator()(T *p) const {} };
struct E {};
template <class T> T &&fwd(T &t) { return static_cast<T &&>(t); }
struct holder {
    int *p;
    [[no_unique_address]] Del<int> d;
    [[__no_unique_address__]] E e;
    void direct(holder &o) { d = o.d; }
    void moved(holder &o) { d = fwd<Del<int>>(o.d); }
    void plain(holder &o) { e = o.e; }
};
int x;
int main() {
    holder a{&x}, b{nullptr};
    a.direct(b); if (a.p != &x) return 1;
    a.moved(b); if (a.p != &x) return 2;
    a.plain(b); if (a.p != &x) return 3;
    Del<int> &r = a.d; r = b.d; if (a.p != &x) return 4;
    return 0;
}
