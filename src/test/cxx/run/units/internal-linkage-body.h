#include "internal-linkage.h"
namespace { struct Anon { static constexpr int value = UNIT; int operator()() const { return UNIT; } };
template <class T> struct Box { static int value() { return UNIT * 3; } };
enum E { e = UNIT }; int avar = UNIT * 7; int afn() { return UNIT * 11; } }
static int sfn() { return UNIT * 13; }
static int svar = UNIT * 17;
template <class T> int of_enum() { return static_cast<int>(T(1)) * UNIT; }
static Node reg1([]() { return UNIT * 19; });
static Node reg2([]() { return apply([] { return UNIT * 23; }); });
static int local_user() { struct L { enum { value = UNIT * 29 }; int operator()() const { return value; } }; L l; return apply(l) + Counter<L>::next() + Holder<L>::call() + vt<Holder<L>>()->v(); }
static Node reg3([]() { return erased([] { static int n; return UNIT * 37 + n++; }); });
static auto glambda = [] { return UNIT * 31; };
#if UNIT == 1
int Worker::work(int n)
{
    struct L { enum { value = 5 }; };
    return erased([n] { return n + 1; }) + Counter<L>::next();
}
#endif
int NAME()
{
#if UNIT == 1
    printf("%d\n", Worker().work(3));
#endif
    int r = 0;
    for (Node *n = Node::head; n; n = n->next) { int v = n->fn(); r = r * 3 + v; }
    printf("%d\n", apply(Anon()));
    int a = Holder<Anon>::call(), b = ++Holder<Anon>::count, c = vt<Holder<Anon>>()->v();
    printf("%d %d %d\n", a, b, c);
    printf("%d %d %d %d\n", through<sfn>(), through<afn>(), read<&svar>(), read<&avar>());
    printf("%d %d\n", Counter<Anon>::next(), tt<Box>());
    printf("%d %d %d\n", local_user(), apply(glambda), autov<sfn>() + autov<&afn>());
    printf("%d\n", of_enum<E>());
    printf("%d %d\n", erased(Anon()), erased([] { return UNIT * 41; }));
    return r;
}
