/* [temp.deduct.partial]/12: a constructor templated only by a defaulted
   template template parameter against a forwarding one (libc++'s tuple) */
template <class T> struct Tu {
    int w;
    template <template <class> class A = Tu, int = 0> Tu(const T &) : w(1)
    {
    }
    template <class U, int = 0> Tu(U &&) : w(2)
    {
    }
};
int main()
{
    const int a = 1;
    int b = 2;
    Tu<int> t(a), u(b);
    return !(t.w == 1 && u.w == 2);
}
