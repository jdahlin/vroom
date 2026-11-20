/* [temp.arg.explicit]/9: a member function template with a template parameter pack and a function parameter pack, called through an object and the class name (static) */
struct S {
    template <class... Ts> int count(Ts... ts) const { return int(sizeof...(ts)); }
    template <class T, class... Ts> static int first(T t, Ts...) { return int(t); }
};
struct D : S {};
int main()
{
    D d;
    const S s = {};
    return s.count() + s.count(1, 'a', 2L) - 3 + S::first(7, 1, 2) - 7 + d.first('a') - 'a' + d.count(1) - 1;
}
