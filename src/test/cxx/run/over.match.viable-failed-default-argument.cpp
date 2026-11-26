/* [over.match.viable]/2-3: a constructor of a class template specialization
   whose default argument is ill-formed for its template arguments
   ([temp.inst]/12) is not viable when an argument given has no conversion
   sequence, and does not stop the call */
template <class T> struct list {};
struct compare {
    int *p;
    explicit compare(int *q) : p(q) {}
};
template <class C> struct map {
    int n;
    explicit map(const C &c) : n(*c.p) {}
    map(list<C>, const C &c = C()) : n(0) {}
};

int main()
{
    int v = 7;
    compare c(&v);
    map<compare> m(c);
    return m.n - 7;
}
