/* [expr.const]: a constexpr member function that reads nothing of its
   object may be called on a variable that is no constant (Highway's
   dh.MaxBytes() in a template argument) */
template <class T, int N> struct Simd {
    static constexpr unsigned long Static() { return N * sizeof(T); }
    constexpr unsigned long MaxBytes() const { return N * sizeof(T); }
};
template <unsigned long N> struct V {
    static constexpr unsigned long n = N;
};

template <class D> unsigned long f(D d)
{
    const D dh;
    V<dh.MaxBytes()> a;
    V<d.MaxBytes() + 1> b;
    V<dh.Static()> c;
    return a.n + b.n + c.n;
}

int main() { return f(Simd<int, 4>()) != 49; }
