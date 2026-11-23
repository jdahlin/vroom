/* [temp.mem]/1, [over.match.oper]: member operator templates, called by operator syntax: a binary operator, operator(), operator[], operator-> and a compound assignment, deduced from the other operands */
struct V {
    int v;
    template <class T> V operator+(T t) const { return V{v + int(t)}; }
    template <class T> V &operator+=(T t) { v += int(t); return *this; }
    template <class T> int operator()(T a, T b) { return v + int(a) + int(b); }
    template <class T> int operator[](T i) const { return v * int(i); }
    template <class T> bool operator==(T t) const { return v == int(t); }
};
int main()
{
    V a = {2};
    V b = a + 3;
    b += 'a';
    return b.v - 102 + a(1, 2) - 5 + a[3] - 6 + (a == 2L) - 1;
}
