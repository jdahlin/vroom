// [temp.arg.explicit]/4 with [over.over]: a template-id that names one
// specialization, its other arguments defaulted and the templates of the
// name that substitution rejects removed ([temp.deduct]/8), is a function
// that can be passed on and deduced from.
template <bool B, class T = void> struct enable {};
template <class T> struct enable<true, T> {
    using type = T;
};

template <class T, typename enable<(T(-1) > T(0)), int>::type = 0>
int pick(T v)
{
    return int(v) + 100;
}

template <class T, typename enable<!(T(-1) > T(0)), int>::type = 0>
int pick(T v)
{
    return int(v) + 200;
}

template <class T, int K = 5> int plus(T v) { return int(v) + K; }

template <class F, class T> int apply(F f, T v) { return f(v); }

template <class T> int both() { return apply(pick<T>, T(1)); }

int main()
{
    if (apply(pick<unsigned>, 1u) != 101)
        return 1;
    if (apply(pick<int>, 2) != 202)
        return 2;
    if (both<unsigned char>() != 101 || both<long>() != 201)
        return 3;
    if (apply(plus<int>, 3) != 8)
        return 4;
    return 0;
}
