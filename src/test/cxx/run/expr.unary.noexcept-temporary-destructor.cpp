/* [expr.unary.noexcept]/2, [except.spec]/6.2: the operand is a
   full-expression, which invokes the destructors of its temporaries (a
   by-value argument's too): one that is potentially-throwing makes it so.
   A new-expression's object is no temporary */
extern "C" int printf(const char *, ...);

typedef decltype(sizeof 0) size_type;
void *operator new(size_type, void *p, int) noexcept
{
    return p;
}

struct T {
    int v;
    ~T() noexcept(false)
    {
    }
};
struct U {
    U() noexcept;
    ~U() noexcept(false);
};
struct W {
    T t;
};
struct Q {
    int v;
    ~Q()
    {
    }
};

void f(T) noexcept;
void g(const T &) noexcept;
void g(const Q &) noexcept;
void h(U) noexcept;
void k(W) noexcept;
void q(Q) noexcept;
T make() noexcept;
T &existing() noexcept;
T array_of[2];

template <class X> constexpr bool passes = noexcept(X{});

bool at_namespace_scope = noexcept(f(T{}));

int main()
{
    T t{1};
    char room[sizeof(T)];
    printf("%d %d %d %d %d %d\n", noexcept(f(T{})), noexcept(g(T{})),
           noexcept(h(U{})), noexcept(h(U())), noexcept(g(T())),
           noexcept(k(W{})));
    printf("%d %d %d %d\n", noexcept(T{}), noexcept(f(make())),
           noexcept(make()), at_namespace_scope);
    printf("%d %d %d %d\n", noexcept(f(t)), noexcept(g(t)), noexcept(T{1}.v),
           noexcept((T{}, 1)));
    printf("%d %d %d %d\n", noexcept(existing()), noexcept(g(existing())),
           noexcept(q(Q{})), noexcept(new (room, 0) T{2}));
    static_assert(!noexcept(g(T{})), "");
    static_assert(!passes<T>, "");
    static_assert(passes<Q>, "");
    printf("%d %d\n", passes<T>, passes<Q>);
}
