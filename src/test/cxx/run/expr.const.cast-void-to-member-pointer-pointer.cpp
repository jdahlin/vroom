/* [expr.static.cast]/13, [expr.const.cast]/7: a static_cast from void * to a
   pointer to a pointer to member casts away no constness */
struct S {
    int k;
    int get() { return k; }
};
template <class M> static M load(void *p) { return *static_cast<M *>(p); }
int main()
{
    int (S::*f)() = &S::get;
    int S::*d = &S::k;
    S s{7};
    void *vf = &f;
    void *vd = &d;
    return (s.*load<int (S::*)()>(vf))() != 7 || s.*load<int S::*>(vd) != 7;
}
