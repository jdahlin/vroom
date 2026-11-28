// [over.match.copy]/1.2 with [over.match.best] and [over.ics.rank]/3.2.3:
// a return of a class prvalue converted by a ref-qualified conversion
// function template, && preferred to const& for an rvalue.
template <class A, class B> struct out {
    A in;
    B out_;

    template <class C, class D> constexpr operator out<C, D>() const &
    {
        return {C(in + 100), D(out_)};
    }

    template <class C, class D> constexpr operator out<C, D>() &&
    {
        return {C(in), D(out_)};
    }
};

template <class A, class B> struct raw {
    A first;
    B second;

    template <class C, class D> constexpr operator out<C, D>() &&
    {
        return {C(first), D(second)};
    }
};

static raw<int, int> make() { return {1, 2}; }
static out<int, int> from_raw() { return make(); }
static out<long, long> widened() { return from_raw(); }

int main()
{
    out<int, int> a = from_raw();
    if (a.in != 1 || a.out_ != 2)
        return 1;
    out<long, long> b = widened();
    if (b.in != 1 || b.out_ != 2)
        return 2;
    return 0;
}
