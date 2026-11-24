// [temp.expl.spec]/1, [temp.explicit]/1: an explicit specialization of a
// member constructor or destructor of a class template specialization,
// and an explicit instantiation declaration of them: no decl-specifier-seq.
static int constructed;
static int destroyed;
template <class C> struct G {
    G(const char *);
    ~G();
    int order() const;
    C c;
};
template <> G<char>::G(const char *);
template <> G<char>::~G();
extern template G<long>::G(const char *);
template <class C> G<C>::G(const char *) { constructed += 1; }
template <class C> G<C>::~G() { destroyed += 1; }
template <class C> int G<C>::order() const { return 3; }
template <> G<char>::G(const char *) { c = 'x'; constructed += 100; }
template <> G<char>::~G() { destroyed += 100; }
int main()
{
    {
        G<char> a("a");
        G<int> b("b");
        if (a.c != 'x')
            return 1;
    }
    if (constructed != 101)
        return 2;
    if (destroyed != 101)
        return 3;
    return 0;
}
