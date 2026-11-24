// [dcl.init]/17.6.2, [over.match.ctor], [temp.mem]/1: __is_convertible(X, X)
// for a class with a constructor template, which takes part in the
// copy-initialization of an X from an X.
struct Forwarding {
    Forwarding();
    template <class U> Forwarding(U &&);
};
struct Constrained {
    Constrained();
    Constrained(const Constrained &);
    template <class U> Constrained(U &&, int = 0) = delete;
};
struct Plain {
    Plain();
    Plain(const Plain &);
    template <class U> Plain(U *);
};
struct Explicit {
    Explicit();
    template <class U> explicit Explicit(U &&);
};

static_assert(__is_convertible(Forwarding, Forwarding));
static_assert(__is_convertible(Forwarding &, Forwarding));
static_assert(__is_convertible(const Forwarding &, Forwarding));
static_assert(__is_convertible(Forwarding, const Forwarding &));
static_assert(__is_convertible(int, Forwarding));
static_assert(!__is_convertible(Constrained &, Constrained));
static_assert(__is_convertible(const Constrained &, Constrained));
static_assert(__is_convertible(Plain, Plain));
static_assert(__is_convertible(Plain &, Plain));
static_assert(__is_convertible(Explicit, Explicit));
static_assert(!__is_convertible(int, Explicit));

int main() { return 0; }
