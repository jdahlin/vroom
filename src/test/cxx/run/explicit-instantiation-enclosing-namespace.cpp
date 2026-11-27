/* [temp.explicit]: a static data member of a specialization of a template in
   a nested namespace instantiated from an enclosing one */
extern "C" int printf(const char *, ...);
namespace tools {
template <class T> struct Registry {
    static Registry *gHead;
    Registry *next;
    T v;
    explicit Registry(T x) : next(gHead), v(x) { gHead = this; }
};
template <class T> Registry<T> *Registry<T>::gHead;
}
namespace test {
using R = tools::Registry<int>;
}
template test::R *test::R::gHead;
static test::R a(1), b(2);
int main()
{
    for (test::R *r = test::R::gHead; r; r = r->next)
        printf("%d\n", r->v);
}
