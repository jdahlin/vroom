/* [temp.mem]/2: access control rules apply to member template names: a private member function template is called by members and friends (and through a public one), a protected one by derived classes */
class C {
    template <class T> static T priv(T t) { return t + 1; }
    template <class T> T inst(T t) { return t + 2; }
protected:
    template <class T> T prot(T t) { return t + 3; }
public:
    template <class T> T pub(T t) { return priv(t) + inst(t); }
    friend int via_friend(C &);
};
struct E : C {
    int use() { return prot(1); }
};
int via_friend(C &c)
{
    return C::priv(1) + c.inst(1);
}
int main()
{
    C c;
    E e;
    return c.pub(1) - 5 + e.use() - 4 + via_friend(c) - 5;
}
