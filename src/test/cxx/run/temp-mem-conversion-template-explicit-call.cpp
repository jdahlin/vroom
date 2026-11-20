/* [temp.mem]/5, Example 6: a conversion function template named in an explicit call, a.operator int*() */
struct A {
    template <class T> operator T *() { static T v = T(5); return &v; }
};
int main()
{
    A a;
    int *ip = a.operator int *();
    return *ip - 5;
}
