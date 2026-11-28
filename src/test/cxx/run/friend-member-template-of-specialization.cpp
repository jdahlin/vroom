/* template <bool> friend class tv<V, F>::it; in a member template of tv: the
   head is the member template's own, unnamed (libc++'s transform_view) */
template <class V, class F>
struct tv {
    template <bool> class it;
    template <bool> class se;
    it<false> b() { return {}; }
};
template <class V, class F>
template <bool C>
class tv<V, F>::it {
    template <bool> friend class tv<V, F>::it;
    template <bool> friend class tv<V, F>::se;
    int p = 5;
public:
    int get() { return p; }
};
int main() { tv<int, int> t; return t.b().get() != 5; }
