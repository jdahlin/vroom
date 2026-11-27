/* [temp.deduct.call]/4.3: a class declared and not defined has no base to
   deduce from, so a constructor template taking a specialization's
   reference is no candidate for it */
class Variable;
template <class T, int N = 0> struct raw_ref {
    explicit raw_ref(T &p) : p(&p) {}
    raw_ref(const raw_ref &o) : p(o.p) {}
    template <class U, int M> raw_ref(const raw_ref<U, M> &o) : p(o.p) {}
    template <class U, int M> raw_ref(raw_ref<U, M> &&o) : p(o.p) {}
    T *p;
};
struct Holder {
    explicit Holder(const Variable *v) : f(*v) {}
    const raw_ref<const Variable> f;
};
template <class T> struct Box {};
template <class T> int pick(const Box<T> &) { return 1; }
int pick(const Variable &) { return 2; }
template <class T> int pointed(const Box<T> *) { return 3; }
int pointed(const void *) { return 4; }
int test(const Variable &v) { return pick(v) * 10 + pointed(&v); }
class Variable {
  public:
    int value = 7;
};
int main()
{
    Variable v;
    Holder h(&v);
    if (h.f.p->value != 7)
        return 1;
    if (test(v) != 24)
        return 2;
    return 0;
}
