/* [abi:calls]: a class whose copy and move constructors and destructor are trivial is passed
   in registers.  A constructor template is never a copy or move constructor
   ([class.copy.ctor]/5, [temp.mem]/1; the implicit copy constructor is declared beside it for
   overload resolution), so it leaves the class trivially copyable.  Called through a pointer of
   another type, the value must arrive in the integer register: vcc passed such a class by
   address, which a clang-built caller does not, and a vcc built by vcc crashed when mixed with
   clang's objects (the views of its syntax tree, syntax-views.inc, have constructor templates). */
struct base {
    unsigned n = 0;
    unsigned get() const { return n; }
};
struct plain : base {};
struct with_template : base {
    with_template() = default;
    template <class V> with_template(V v) { n = v.get(); }
};
template <class V> concept has_get = requires(V v) { v.get(); };
struct constrained : base {
    constrained() = default;
    template <class V>
        requires has_get<V>
    constrained(V v) { n = v.get(); }
};
struct with_reference_template : base {
    with_reference_template() = default;
    template <class V> with_reference_template(const V &v) { n = v.get(); }
};
struct nontrivial {  /* passed by address in both ABIs: the copy is user-provided */
    unsigned n = 0;
    nontrivial() = default;
    nontrivial(const nontrivial &o) : n(o.n) {}
};

static __attribute__((noinline)) unsigned plain_in(plain x) { return x.get(); }
static __attribute__((noinline)) unsigned template_in(with_template x) { return x.get(); }
static __attribute__((noinline)) unsigned constrained_in(constrained x) { return x.get(); }
static __attribute__((noinline)) unsigned reference_template_in(with_reference_template x) { return x.get(); }
static __attribute__((noinline)) unsigned nontrivial_in(nontrivial x) { return x.n; }

int main()
{
    typedef unsigned (*in_register)(unsigned);
    int bad = 0;
    bad += reinterpret_cast<in_register>(&plain_in)(7) != 7;
    bad += reinterpret_cast<in_register>(&template_in)(8) != 8;
    bad += reinterpret_cast<in_register>(&constrained_in)(9) != 9;
    bad += reinterpret_cast<in_register>(&reference_template_in)(11) != 11;
    /* a class with a user-provided copy constructor is passed by address: the same calls, by value */
    nontrivial a;
    a.n = 12;
    bad += nontrivial_in(a) != 12;
    return bad;
}
