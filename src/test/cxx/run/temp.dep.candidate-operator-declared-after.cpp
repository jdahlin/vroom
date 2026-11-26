/* [temp.res]/10, [temp.dep.candidate]/1: an operator in a template is
   looked up among the operator functions declared before the template, and
   by argument-dependent lookup; one declared after it does not stop the
   instantiation (doctest's XmlWriter) */
namespace io {
struct stream {
    int n;
};
struct a {};
} // namespace io
namespace out {
using io::a;
using io::stream;
namespace inner {
struct b {};
} // namespace inner
stream &operator<<(stream &s, const a &)
{
    s.n += 1;
    return s;
}
template <class T> int write(const T &t)
{
    stream s{0};
    s << t;
    s << a();
    return s.n;
}
namespace inner {
stream &operator<<(stream &s, const b &)
{
    s.n += 10;
    return s;
}
} // namespace inner
stream &operator<<(stream &s, int v)
{
    s.n += v * 1000;
    return s;
}
} // namespace out

int main()
{
    /* b's operator is found by argument-dependent lookup */
    return out::write(out::inner::b()) - 11;
}
