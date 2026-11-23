/* [temp.mem]/1, [class.ctor]: a constructor template declared in the class and defined outside it, deduced from the arguments */
struct S {
    int v;
    template <class T> S(T t);
};
template <class T> S::S(T t) : v(int(t) * 2) {}
int main()
{
    S a(3);
    S e(2.5);
    return a.v - 6 + e.v - 4;
}
