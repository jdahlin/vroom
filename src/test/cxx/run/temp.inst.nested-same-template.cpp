/* A specialization of a template nested in itself, instantiated while the
   outer one's members are declared: each one's deduced-return members are
   its own ([temp.inst]/4, [dcl.spec.auto]). */
template <class T> struct A {
    T d[2];
    auto &operator[](int i) { return d[i]; }
    const auto &operator[](int i) const { return d[i]; }
    const auto &at() const { return d[0]; }
};

using X = A<A<A<A<short>>>>;

template <int K> int get(const X *x) { return (*x)[1][1][1][K]; }

int main()
{
    X x = {};
    x[1][1][1][1] = 5;
    x[0][0][0][0] = 2;
    const X &c = x;
    if (get<1>(&x) != 5)
        return 1;
    if (c.at().at().at().at() != 2)
        return 2;
    return 0;
}
