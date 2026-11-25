/* [dcl.init.list]/5: the elements of an initializer_list's array are the
   objects the program sees, and the ones destroyed, once each */
typedef decltype(sizeof 0) size_type;
namespace std
{
template <class T> class initializer_list
{
    const T *p;
    size_type n;

public:
    constexpr const T *begin() const
    {
        return p;
    }
    constexpr const T *end() const
    {
        return p + n;
    }
};
} // namespace std
int made, gone, bad;
struct R {
    mutable int *p;
    const R *self;
    R(int x) : p(new int(x)), self(this)
    {
        made++;
    }
    ~R()
    {
        gone++;
        if (self != this)
            bad++;
        delete p;
    }
};
int take(std::initializer_list<R> l)
{
    int sum = 0;
    for (const R &r : l) {
        sum = sum * 10 + *r.p;
        if (r.self != &r)
            bad++;
        delete r.p;
        r.p = nullptr;
    }
    return sum;
}
int main()
{
    int sum = take({1, 2, 3});
    return !(sum == 123 && made == 3 && gone == 3 && !bad);
}
