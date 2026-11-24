// [temp.deduct.call]/4.3: a parameter that is a class template-id cannot be
// deduced from an argument of array or scalar type, which only makes the
// template no candidate (the call then selects another overload) -- here
// the append of a string literal beside a template taking a Box<T>.
template <class T> struct Box {
    T v;
};
struct S {
    int value = 0;
    template <class T> void put(const Box<T> &b) { value += b.v; }
    void put(const char *) { value += 100; }
    void put(int x) { value += x; }
};
template <class X> int use(X &&x)
{
    S s;
    s.put(static_cast<X &&>(x));
    return s.value;
}
int main()
{
    if (use("abc") != 100)
        return 1;
    if (use(5) != 5)
        return 2;
    if (use(Box<int>{7}) != 7)
        return 3;
    return 0;
}
