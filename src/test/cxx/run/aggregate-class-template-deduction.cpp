/* [over.match.class.deduct]/1: the aggregate deduction candidate, an element
   for a base and one for a member of a template parameter's type */
struct base {
    int &value;
};
template <class P> struct filler : base {
    P proj;
    int fill(int x) const
    {
        value = x + proj;
        return value;
    }
};
template <class A, class B> struct pair {
    A a;
    B b;
};
int main()
{
    int d = 0;
    filler f{{d}, 3};
    pair p{1, 'x'};
    return f.fill(4) != 7 || d != 7 || p.a != 1 || p.b != 'x' ||
           sizeof(p.b) != 1;
}
