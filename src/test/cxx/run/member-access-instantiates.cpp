/* [temp.inst]/2: a member access whose type is first completed there: the
   instantiation declares many entities while the member is being read */
template <int N> struct Many {
    template <int K> struct Pick { static constexpr int value = K + N; };
    int a = Pick<0>::value, b = Pick<1>::value, c = Pick<2>::value;
    int d = Pick<3>::value, e = Pick<4>::value, f = Pick<5>::value;
    int sum() const { return a + b + c + d + e + f; }
};

template <int N> struct Holder {
    Many<N> *many;
};

template <int N> int through_lambda(Holder<N> &h, Many<N> *m)
{
    h.many = m;
    auto get = [&h]() -> Many<N> & { return *h.many; };
    return get().sum();
}

static int by_reference(const Holder<7> &h)
{
    auto get = [&h]() { return h.many == nullptr; };
    return get();
}

int main()
{
    Many<1> m;
    Holder<1> h{};
    Holder<7> empty{};
    if (through_lambda(h, &m) != 21)
        return 1;
    if (by_reference(empty) != 1)
        return 2;
    return 0;
}
