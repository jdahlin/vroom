/* [temp.mem]/1, [temp.alias]: a member alias template of a class and of a class template, used in a member function template's signature */
template <class P> struct H {
    template <class T> using pair_of = P (*)(T);
    template <class T> static P apply(pair_of<T> f, T t) { return f(t); }
};
int twice(int x) { return x * 2; }
int main()
{
    return H<int>::apply<int>(twice, 4) - 8 + H<int>::apply(twice, 1) - 2;
}
