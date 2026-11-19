/* [temp.deduct.guide]/1: deduction guides are declared for a class
   template (libc++'s pair); they are no functions and get no code.  Class
   template argument deduction ([over.match.class.deduct]) is not
   implemented, so the guides are only recorded. */
template <class A, class B> struct pair {
    A first;
    B second;
    pair(A a, B b) : first(a), second(b) {}
};
template <class T1, class T2> pair(T1, T2) -> pair<T1, T2>;
template <class T> struct box {
    T value;
};
explicit box(int) -> box<long>;
int main()
{
    pair<int, double> p(1, 2.0);
    box<long> b = {3};
    return p.first + (int)p.second + (int)b.value - 6;
}
