// [temp.mem]/1: out-of-class definition of a member function/constructor
// template of a non-template nested class of a class template (e.g.
// libc++'s `discrete_distribution<_IntType>::param_type::param_type`).

template <class T> struct Dist {
    struct Param {
        T sum;
        template <class F> Param(T x, F f);
    };
};

template <class T>
template <class F>
Dist<T>::Param::Param(T x, F f) : sum(f(x))
{
}

int main()
{
    return 0;
}
