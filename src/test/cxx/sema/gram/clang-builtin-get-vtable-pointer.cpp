// __builtin_get_vtable_pointer: a pointer (or array) to a polymorphic class, a const void *
struct P {
    virtual ~P();
};
struct Q : P {};
struct V : virtual P {};
P p;
P pa[2];
extern const P cp;
template <class A, class B> struct same { static constexpr bool value = false; };
template <class A> struct same<A, A> { static constexpr bool value = true; };
static_assert(same<decltype(__builtin_get_vtable_pointer(&p)), const void *>::value);
static_assert(same<decltype(__builtin_get_vtable_pointer(pa)), const void *>::value);
static_assert(same<decltype(__builtin_get_vtable_pointer(&cp)), const void *>::value);
static_assert(same<decltype(__builtin_get_vtable_pointer((Q *)0)), const void *>::value);
static_assert(same<decltype(__builtin_get_vtable_pointer((V *)0)), const void *>::value);
const void *f(P *x) { return __builtin_get_vtable_pointer(x); }
struct N {};
N n;
struct F;
void bad(F *fd, int i)
{
    __builtin_get_vtable_pointer(0);
    __builtin_get_vtable_pointer(p);
    __builtin_get_vtable_pointer(&n);
    __builtin_get_vtable_pointer(fd);
    __builtin_get_vtable_pointer(i);
}
