/* an auto member function returning a member class that is defined outside
   the class template is deduced where it is used (libc++'s filter_view) */
template <class V> struct cat {};
template <class V> struct fv {
    V base_;
    class it;
    class se;
    constexpr auto end() {
        if constexpr (sizeof(V) == 4)
            return it{*this};
        else
            return se{*this};
    }
};
template <class V> class fv<V>::it : public cat<V> {
public:
    fv *p;
    it(fv &f) : p(&f) {}
};
template <class V> class fv<V>::se {
public:
    se(fv &) {}
};
int main() { fv<int> f{7}; return f.end().p->base_ != 7; }
