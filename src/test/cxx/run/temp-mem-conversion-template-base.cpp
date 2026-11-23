/* [temp.mem]/6-7, [class.conv.fct]: a conversion function template inherited from a base class, found in the context of the use, not by name */
struct B {
    template <class T> operator T() const { return T(3); }
};
struct D : B {};
int main()
{
    D d;
    int i = d;
    long l = d;
    return i - 3 + int(l) - 3;
}
