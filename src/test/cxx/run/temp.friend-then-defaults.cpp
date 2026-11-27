/* [temp.friend], [dcl.fct.default]/4: a definition adds default arguments to
   a function template only friend declarations declared before, and a
   friend declaration after it leaves it visible and its defaults
   (libc++'s regex_search and regex_match). */
extern "C" int printf(const char *, ...);

template <class C> struct regex {
    C c = 'a';
    template <class B, class X>
    friend int search(B, B, const regex<X> &, int);
    template <class X> int run() const { return search(&c, &c, *this); }
};

template <class B, class C>
int search(B, B, const regex<C> &r, int f = 5)
{
    return f + r.c;
}

template <class C> struct results {
    template <class B, class X>
    friend int search(B, B, const regex<X> &, int);
};

int main()
{
    regex<char> r;
    results<int> m;
    (void)m;
    printf("%d %d\n", r.run<int>(), search(&r.c, &r.c, r));
    return 0;
}
