/* [temp.mem]/5-6: a conversion function template converting to pointers, defined in the class, deduced by the target type of an initialization ([temp.deduct.conv]) */
struct A {
    template <class T> operator T *() { return nullptr; }
    template <class T> operator T() const { return T(7); }
};
int main()
{
    A a;
    int *ip = a;
    long n = a;
    return (ip != nullptr) + int(n) - 7;
}
