/* [temp.mem]/1, [temp.mem.class]: a member class template of a class template, used through the specialization of the enclosing class */
template <class P> struct H {
    template <class T> struct In {
        P p;
        T t;
        int sum() const { return int(p) + int(t); }
    };
};
int main()
{
    H<int>::In<char> c = {1, 2};
    return c.sum() - 3;
}
