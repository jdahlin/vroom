/* [temp.mem]/1, [temp.mem.func]/1: member function templates declared in a
   class or class template and defined outside it */
template <class M>
struct U {
    M value;
    template <class R>
    bool bigger(const R &r);
    template <class R, class S>
    M mix(R r, S s) const;
    template <class R>
    int twice(R r) const;
    template <class R>
    M operator()(R r, int k);
    template <class R>
    void set(R r);
};

template <class M>
template <class R>
bool U<M>::bigger(const R &r)
{
    return sizeof(R) > sizeof(M) && value < M();
}

template <class M>
template <class R, class S>
M U<M>::mix(R r, S s) const
{
    return static_cast<M>(r) + static_cast<M>(s) + value;
}

template <class M>
template <class R>
int U<M>::twice(R r) const
{
    return static_cast<int>(r) * 2 + static_cast<int>(sizeof(M)) + value;
}

template <class M>
template <class R>
M U<M>::operator()(R r, int k)
{
    return value + static_cast<M>(r) + k;
}

template <class M>
template <class R>
void U<M>::set(R r)
{
    value = static_cast<M>(r);
}

int main()
{
    U<int> a;
    a.value = -1;
    U<char> b;
    b.value = 1;
    int sum = 0;
    sum += a.bigger(1.0);
    sum += b.bigger(1.0) * 2;
    sum += a.mix(1, 2L);
    sum += b.twice(5);
    a.set(4.5);
    sum += a(1.0, 2);
    if (sum != 7 + 1 + 2 + (1 + 2 - 1) + (10 + 1 + 1))
        return 1;
    return 0;
}
