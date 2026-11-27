/* [expr.static.cast]/2: a downcast to a specialization not instantiated
   before the cast (libc++'s __hash_node_base::__hash). */
extern "C" int printf(const char *, ...);

template <class T> struct node;
template <class T> struct base {
    typedef node<T> node_type;
    int hash() const
    {
        return static_cast<node_type const &>(*this).value;
    }
};
template <class T> struct node : base<T> {
    T value = 7;
};

int read(const base<int> &b) { return b.hash(); }

int main()
{
    node<int> n;
    printf("%d\n", read(n));
    return 0;
}
