/* [temp.mem]/8, [temp.func.order]: overload resolution and partial ordering select among conversion function templates and a non-template conversion function */
struct A {
    operator int() const { return 1; }
    template <class T> operator T() const { return T(2); }
    template <class T> operator T *() const { return nullptr; }
};
int main()
{
    A a;
    int i = a;
    long l = a;
    double d = a;
    char *p = a;
    return i - 1 + int(l) - 2 + int(d) - 2 + (p != nullptr);
}
