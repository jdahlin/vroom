// [temp.mem.func]: out-of-class definition of operator= of a nested class of
// a class template (e.g. libc++'s piecewise_constant_distribution::param_type::operator=).

template <class T> struct Dist {
    struct Param {
        T v;
        Param &operator=(const Param &rhs);
    };
};

template <class T>
typename Dist<T>::Param &Dist<T>::Param::operator=(const Param &rhs)
{
    v = rhs.v;
    return *this;
}

int main()
{
    Dist<int>::Param a{1};
    Dist<int>::Param b{42};
    a = b;
    if (a.v != 42)
        return 1;
    return 0;
}
