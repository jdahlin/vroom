/* [temp.mem]/4, Example 5: a specialization of a member function template does not override a virtual function of a base class; the overriding non-template calls the specialization */
struct B {
    virtual int f(int) { return 1; }
};
struct D : B {
    template <class T> int f(T) { return 2; }
    int f(int i) override { return f<>(i) + 10; }
};
int main()
{
    D d;
    B &b = d;
    return b.f(0) - 12 + d.f<char>('x') - 2 + d.B::f(0) - 1;
}
