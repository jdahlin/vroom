/* [temp.mem]/1, Example 1: member templates of a class template, defined in the class (a converting constructor template) and outside it with the template-head of the class followed by the member's */
template <class T> struct string {
    T first = T();
    string() = default;
    template <class T2> int compare(const T2 &);
    template <class T2> string(const string<T2> &s) : first(T(s.first)) {}
    template <class T2> static T2 twice(T2 t) { return t + t; }
    template <class T2, class T3> T2 mix(T2 a, T3 b);
};
template <class T> template <class T2> int string<T>::compare(const T2 &s)
{
    return int(first) - int(s) + 10;
}
template <class T> template <class T2, class T3> T2 string<T>::mix(T2 a, T3 b)
{
    return a + T2(b) + T2(first);
}
int main()
{
    string<int> a;
    a.first = 4;
    string<long> b(a);
    int r = a.compare(4) - 10 + b.compare(short(4)) - 10;
    r += string<int>::twice(3) - 6 + a.mix(1, 2.0) - 7 + b.mix<int, char>(1, 2) - 7;
    return r + int(b.first) - 4;
}
