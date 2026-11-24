/* [class.mfct]/1, [temp.mem.func]/1: the constructor, destructor and a
   function of a member class of a class template, defined outside the
   class template (template<class T> B<T>::S::S(...) {...}) */
template <class T> struct B {
    struct S {
        S(B<T> &b);
        ~S();
        int get() const;
        int ok;
        B<T> *p;
    };
    int v;
};

template <class T> B<T>::S::S(B<T> &b) : ok(0), p(&b)
{
    if (b.v)
        ok = 1;
}

template <class T> B<T>::S::~S()
{
    ok = 2;
}

template <class T> int B<T>::S::get() const
{
    return ok + p->v;
}

int main()
{
    B<int> b;
    b.v = 1;
    B<int>::S s(b);
    if (s.ok != 1)
        return 1;
    if (s.get() != 2)
        return 2;
    B<long> c;
    c.v = 0;
    B<long>::S t(c);
    return t.ok;
}
