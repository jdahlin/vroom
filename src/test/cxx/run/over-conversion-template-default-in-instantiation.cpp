/* [temp.deduct.conv], [temp.deduct.general]/5: a conversion function
   template with a defaulted template parameter is a candidate inside a
   function template's instantiation too (json's
   static_cast<string_t>(value.at(key)) in binary_writer) */
struct Str {
    int n;
    Str() : n(0)
    {
    }
    Str(int v) : n(v)
    {
    }
    Str(const Str &o) : n(o.n)
    {
    }
    Str(Str &&o) : n(o.n)
    {
    }
};
struct J {
    template <class V, int N = 7> operator V() const
    {
        return V(N);
    }
};
template <class T> int g(const T &value)
{
    Str s = static_cast<Str>(value);
    return s.n;
}
int main()
{
    J j;
    if (g(j) != 7)
        return 1;
    return 0;
}
