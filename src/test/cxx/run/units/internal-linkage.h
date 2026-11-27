extern "C" int printf(const char *, ...);
struct Node {
    typedef int (*Fn)();
    Fn fn;
    Node *next;
    static inline Node *head = nullptr;
    Node(Fn f) : fn(f), next(head) { head = this; }
};
template <class F> struct Holder { static int call() { F f; return f(); } static inline int count = 0; virtual int v() { F f; return f(); } };
template <class F> int apply(F f) { return f(); }
template <int (*P)()> int through() { return P(); }
template <int *P> int read() { return *P; }
template <class T> struct Counter { static int next() { static int n = 0; return ++n * 100 + T::value; } };
template <template <class> class TT> int tt() { return TT<int>::value(); }
template <auto V> int autov() { return V(); }
template <class T> T *vt() { static T t; return &t; }
template <class F> struct Erased {
    struct Base { virtual int run() = 0; };
    struct Impl : Base { F f; Impl(F g) : f(g) {} int run() override { static int calls; return f() + calls++; } };
};
template <class F> int erased(F f) { static typename Erased<F>::Impl impl(f); typename Erased<F>::Base *b = &impl; return b->run(); }
/* a specialization with a member class of a unit's own specialization */
template <class T> struct Outer { struct Member { int v; }; };
template <class T> struct Inner { static int get(T t) { static int n; return t.v + n++; } };
/* closure types of a member function that is not inline: no other unit's */
struct Worker { int work(int n); };
