/* [temp.mem]/1, [temp.pre]/2: a static data member template declared in the class and defined outside it, "template <class T> const T S::one" */
struct S {
    template <class T> static const T one;
};
template <class T> const T S::one = T(1);
int main()
{
    return int(S::one<char>) - 1 + int(S::one<long>) - 1;
}
