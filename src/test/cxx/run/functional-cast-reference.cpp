/* [expr.type.conv]/2: T(e) with T a reference or a pointer to member is the
   cast expression (T)e (libc++'s _Comp_ref(__comp) in __introsort) */
struct counting_less {
    int calls;
    bool operator()(int a, int b)
    {
        ++calls;
        return a < b;
    }
};
struct pair_of_ints {
    int first, second;
    int sum() const { return first + second; }
};

template <class T> struct reference_to {
    typedef T &type;
};

template <class Compare> bool compare_through(Compare &comp)
{
    typedef typename reference_to<Compare>::type comp_ref;
    (void)comp_ref(comp)(1, 2);
    return comp_ref(comp)(2, 1);
}

typedef int pair_of_ints::*member;
typedef int (pair_of_ints::*method)() const;

int main()
{
    counting_less less = {0};
    if (compare_through(less) || less.calls != 2)
        return 1;
    typedef counting_less &ref;
    ref(less).calls = 7;
    if (less.calls != 7 || &ref(less) != &less)
        return 2;
    typedef int &&rvalue;
    int value = 3;
    int &&bound = rvalue(value);
    if (&bound != &value)
        return 3;
    pair_of_ints p = {4, 5};
    if (p.*member(&pair_of_ints::second) != 5)
        return 4;
    if ((p.*method(&pair_of_ints::sum))() != 9)
        return 5;
    return 0;
}
