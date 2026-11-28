/* [class.abstract]: a pure virtual function whose declarator instantiates a
   class template stays pure (Catch's IExceptionTranslator). */
template <class T> struct list {
    struct node {
        T v;
    };
    node n;
    T get() const { return n.v; }
    typedef node *iterator;
};
typedef list<int> ints;

struct I {
    virtual ~I();
    virtual int f(ints::iterator it) const = 0;
};
I::~I() {}
struct J : I {
    int f(ints::iterator it) const override { return it->v; }
};

int main()
{
    static_assert(__is_abstract(I));
    ints x;
    x.n.v = 4;
    J j;
    const I &i = j;
    return i.f(&x.n) != 4;
}
