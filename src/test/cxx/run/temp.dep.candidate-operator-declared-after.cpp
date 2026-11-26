/* [temp.res]/10, [temp.dep.candidate]/1: an operator in a template is
   looked up among the operator functions declared before the template, and
   by argument-dependent lookup; one declared after it does not stop the
   instantiation (doctest's XmlWriter) */
namespace out {
struct stream {
    int n;
};
struct a {};
struct b {};
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
stream &operator<<(stream &s, const b &)
{
    s.n += 10;
    return s;
}
stream &operator<<(stream &s, int v)
{
    s.n += v * 1000;
    return s;
}
} // namespace out

int main()
{
    /* b's operator is found by argument-dependent lookup */
    return out::write(out::b()) - 11;
}
