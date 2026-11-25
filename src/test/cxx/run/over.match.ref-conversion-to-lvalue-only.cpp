/* [over.match.ref]/1: a bool& parameter takes no class whose conversion
   functions all yield prvalues; one yielding an lvalue binds it */
struct J {
    template <class V> operator V() const
    {
        return V();
    }
};
struct K {
    bool b = false;
    operator bool &()
    {
        return b;
    }
};
void g(const J &, bool &) {}
void g(const J &, double &) {}
template <class A, class V> int has(...)
{
    return 0;
}
template <class A, class V,
          class = decltype(g(*static_cast<A *>(nullptr),
                             *static_cast<V *>(nullptr)))>
int has(int)
{
    return 1;
}
void set(bool &r)
{
    r = true;
}
int main()
{
    K k;
    set(k);
    return !(k.b && has<J, J>(0) == 0 && has<J, bool>(0) == 1);
}
