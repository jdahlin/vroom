/* [temp.variadic]/5, [over.call.object]: a call of a class object with a
   pack expansion as its arguments; with covariant clone ([class.virtual]/8) */
template<typename R, typename... A> struct ib {
    virtual ~ib() {}
    virtual R invoke(A...) = 0;
    virtual ib *clone() = 0;
};
template<typename F, typename R, typename... A> struct fi : ib<R, A...> {
    explicit fi(const F &g) : f(g) {}
    R invoke(A... a) { return f(a...); }
    fi *clone() { return new fi(f); }
    F f;
};
struct add {
    int operator()(int x, int y) { return x + y; }
};
int main()
{
    ib<int, int, int> *p = new fi<add, int, int, int>(add());
    ib<int, int, int> *q = p->clone();
    int r = q->invoke(1, 2);
    delete p;
    delete q;
    return r - 3;
}
