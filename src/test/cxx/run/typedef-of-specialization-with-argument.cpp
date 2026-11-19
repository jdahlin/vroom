/* [dcl.typedef]/2: a typedef-name of a class template specialization whose
   template-argument-list holds a template-id names the specialization's
   class, so its members are found through it (libc++'s
   typedef duration<long, ratio<3600>> hours; hours::rep). */
template <long N> struct ratio {};
template <class R, class P> struct duration {
    typedef R rep;
    R r;
    explicit duration(R v) : r(v) {}
};
namespace chrono {
typedef duration<long, ratio<3600>> hours;
}
int main()
{
    chrono::hours::rep a = 40;
    chrono::hours h(static_cast<chrono::hours::rep>(a + 2));
    return (int)h.r - 42;
}
