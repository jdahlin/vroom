/* [temp.inst]/4, [basic.def.odr]/4: naming an overload set of a class
   template specialization instantiates the function overload resolution
   chooses, not the first declared: Stack<Only>::push(const T &) would be
   ill-formed for a move-only T (libc++'s vector<T>::push_back) */
struct Only {
    int n;
    explicit Only(int n_) : n(n_) {}
    Only(Only &&o) : n(o.n) {}
};
template <class T> struct Stack {
    int total = 0;
    void push(const T &t)
    {
        T copy(t);
        total += copy.n;
    }
    void push(T &&t)
    {
        T moved(static_cast<T &&>(t));
        total += moved.n;
    }
};
struct Holder {
    int fill()
    {
        Stack<Only> s;
        s.push(Only(3));
        return s.total;
    }
};
int main()
{
    Holder h;
    return h.fill() != 3;
}
