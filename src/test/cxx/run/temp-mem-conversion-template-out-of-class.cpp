/* [temp.mem]/5, Example 6: a conversion function template defined outside its class and explicitly specialized, "template <> A::operator char*()" */
struct A {
    template <class T> operator T *();
};
template <class T> A::operator T *()
{
    static T v = T(5);
    return &v;
}
template <> A::operator char *()
{
    static char c = 'c';
    return &c;
}
int main()
{
    A a;
    int *ip = a;
    char *cp = a;
    return *ip - 5 + (*cp - 'c');
}
