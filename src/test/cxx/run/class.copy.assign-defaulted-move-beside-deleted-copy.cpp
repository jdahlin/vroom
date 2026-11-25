/* [class.copy.assign]/3, [dcl.fct.def.default]: X& operator=(X&&) =
   default beside X& operator=(const X&) = delete: an rvalue of X is
   assigned by the defaulted move assignment operator, which overload
   resolution prefers ([over.ics.rank]/3.2.3); only an lvalue would name
   the deleted copy.  Stockfish's NumaConfig (cfg = empty(); config =
   std::move(cfg)), its members a std::vector and a std::set; here one
   trivial class and one whose member moves by its own operator. */
template <class T> T &&move(T &x) { return static_cast<T &&>(x); }
struct plain {
    plain() = default;
    plain(const plain &) = delete;
    plain(plain &&) = default;
    plain &operator=(const plain &) = delete;
    plain &operator=(plain &&) = default;
    int v = 0;
};
int moves = 0;
struct owner {
    int *p = nullptr;
    owner() = default;
    owner(owner &&o) : p(o.p) { o.p = nullptr; }
    owner &operator=(owner &&o)
    {
        moves++;
        p = o.p;
        o.p = nullptr;
        return *this;
    }
};
struct config {
    config() = default;
    config(const config &) = delete;
    config(config &&) = default;
    config &operator=(const config &) = delete;
    config &operator=(config &&) = default;
    owner nodes;
    int count = 0;
    static config empty() { return config(); }
};
plain make(int v)
{
    plain p;
    p.v = v;
    return p;
}
int main()
{
    plain a;
    a = make(3);
    plain b;
    b = move(a);
    if (b.v != 3)
        return 1;
    int x = 7;
    config cfg;
    cfg.nodes.p = &x;
    cfg.count = 2;
    config kept;
    kept = move(cfg);
    if (kept.nodes.p != &x || cfg.nodes.p || kept.count != 2 || moves != 1)
        return 2;
    auto fallback = [&]() { kept = config::empty(); };
    fallback();
    if (kept.nodes.p || kept.count != 0 || moves != 2)
        return 3;
    return 0;
}
