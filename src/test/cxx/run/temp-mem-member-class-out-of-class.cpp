/* [temp.mem]/1, [temp.mem.class]: a member class template declared in the class and defined outside it, "template <class T> struct O::Out" */
struct O {
    template <class T> struct Out;
};
template <class T> struct O::Out {
    T w;
    int twice() const { return int(w) * 2; }
};
int main()
{
    O::Out<char> b = {'a'};
    return b.twice() - 194;
}
