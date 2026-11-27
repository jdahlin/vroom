/* [temp.arg.nontype]/2, [class.static]: the address of a static member
   function or static data member as a template argument. */
struct B {
    static int n;
    static int a[3];
    static void inc(int *p);
};
int B::n = 5;
int B::a[3] = {1, 2, 3};
void B::inc(int *p) { ++*p; }

template <auto F> struct Fn {
    void operator()(int *p) const { F(p); }
};
using P = void (*)(int *);
template <P F> void call(int *p) { F(p); }
using R = void (&)(int *);
template <R F> void callr(int *p) { F(p); }
template <int *P> int get() { return *P; }
template <int &R> void bump() { ++R; }
template <int *P> int third() { return P[2]; }

struct C : B {
    static int run(int *p)
    {
        call<inc>(p);
        return get<&n>();
    }
};

int object_test();

int main()
{
    int v = 0;
    Fn<B::inc>{}(&v);
    Fn<&B::inc>{}(&v);
    call<B::inc>(&v);
    call<&B::inc>(&v);
    callr<B::inc>(&v);
    if (v != 5)
        return 1;
    bump<B::n>();
    if (get<&B::n>() != 6 || third<B::a>() != 3)
        return 2;
    if (C::run(&v) != 6 || v != 6)
        return 3;
    if (object_test() != 2)
        return 4;
    return 0;
}

/* The parameter's name as a value: a callee, a decltype operand, another
   template's argument */
template <class T> struct remove_pointer { using type = T; };
template <class T> struct remove_pointer<T *> { using type = T; };
template <typename T, T *P> struct Object {
    template <typename... A>
    auto operator()(A &&...a) const -> decltype(P(a...))
    {
        return P(a...);
    }
};
template <auto F>
using FunctionObject =
    Object<typename remove_pointer<decltype(F)>::type, F>;

int taken(int x) { return x; }

template <int &R> struct Referred {
    template <class A> auto f(A a) -> decltype(taken(R + a))
    {
        return taken(R + a);
    }
};

int object_test()
{
    if (Referred<B::n>{}.f(1) != B::n + 1)
        return 0;
    int v = 0;
    FunctionObject<B::inc>{}(&v);
    FunctionObject<&B::inc> o;
    o(&v);
    return v;
}
