/* [temp.deduct]/5, [temp.mem]/1: a member function template whose template parameter has a default template argument and whose function parameter a default argument; the template parameter deduced from no argument ([temp.deduct.call]/1) */
struct S {
    template <class T = int> T dflt(T t = T(4)) const { return t; }
    template <class T, class U = T> U other(T t) const { return U(t) + 1; }
};
int main()
{
    const S s = {};
    return s.dflt() - 4 + s.dflt(5) - 5 + s.dflt<char>() - 4 + s.other(1) - 2 + s.other<int, long>(1) - 2;
}
