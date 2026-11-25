/* [temp.inst]/4: the definition of a member function of a class template
   specialization is instantiated only when it is needed; an operator
   function's out-of-class definition too.  libc++'s vector::operator= (an
   out-of-class definition calling assign) is ill-formed for
   vector<unique_ptr<T>>, which a program declares without copying it
   (Stockfish's tune.h). */
struct only_moved {
    only_moved() = default;
    only_moved(const only_moved &) = delete;
    only_moved(only_moved &&) = default;
};
template <class T> struct holder {
    T *p = nullptr;
    int n = 0;
    holder &operator=(const holder &x);
    holder &operator+=(const holder &x);
    template <class U> holder &operator<<(U u);
};
/* each would refer to a deleted function for T = only_moved */
template <class T> holder<T> &holder<T>::operator=(const holder &x)
{
    T copy(*x.p);
    n = x.n;
    return *this;
}
template <class T> holder<T> &holder<T>::operator+=(const holder &x)
{
    T copy(*x.p);
    n += x.n;
    return *this;
}
template <class T> template <class U> holder<T> &holder<T>::operator<<(U u)
{
    T copy(*p);
    n += u;
    return *this;
}
holder<only_moved> unused;
/* ... and needed for T = int: instantiated where used */
int main()
{
    int a = 1, b = 2;
    holder<int> x, y;
    x.p = &a;
    y.p = &b;
    x.n = 3;
    y.n = 4;
    x = y;
    x += y;
    x << 5;
    if (x.n != 13)
        return 1;
    return 0;
}
