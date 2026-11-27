/* Declarations the parser once kept as tokens. */
extern "C" int printf(const char *, ...);

/* [ext:gnu_attributes] attributes after a virt-specifier */
struct lockable {
    virtual int lock() { return 1; }
    virtual int unlock() { return 2; }
};
struct locked : lockable {
    int lock() override __attribute__((unused));
    int unlock() override __attribute__((unused)) { return 4; }
};
int locked::lock() { return 3; }

/* [class.member.lookup]: a member type of a template argument the
   specialization derives from */
struct canvas {
    enum strategy { none, full };
    virtual strategy get() { return none; }
};
template <class Base> struct enforcer : public Base {};
struct no_draw : enforcer<canvas> {
    strategy get() override;
};
no_draw::strategy no_draw::get() { return full; }

/* [dcl.type.elab]: class-key identifier ; declares in this scope */
namespace sl {
struct program;
}
namespace sl::rp {
class program;
}
struct holder {
    const sl::rp::program *p;
    int n;
};

/* [dcl.enum]/3: an opaque redeclaration keeps the enumerators */
enum class mode { a, b };
enum class mode;
struct taker {
    int v;
    taker(mode m) : v((int)m) {}
};

/* 'new struct s;' names the class: it declares no local one */
struct node {
    int a;
};
static int fresh(int node)
{
    struct node *p = new struct node;
    p->a = node;
    int a = p->a;
    delete p;
    return a;
}

int main()
{
    locked l;
    lockable *base = &l;
    no_draw d;
    canvas *c = &d;
    holder h = {nullptr, 5};
    taker t(mode::b);
    printf("%d %d %d %d %d\n", base->lock(), base->unlock(), (int)c->get(),
           h.n, t.v);
    if (fresh(6) != 6)
        return 1;
    return !(base->lock() == 3 && base->unlock() == 4 && c->get() == 1 &&
             h.n == 5 && t.v == 1);
}
