/* [dcl.spec.auto]/11, [temp.inst]/2: a member of a class template's
   specialization whose body needs the placeholder return type of members
   declared further down, overloaded ones too (libc++'s take_view::end
   calling size). */
template <class T>
struct View {
    T n;
    auto end() const
    {
        auto s = size();
        return s + 1;
    }
    auto begin() const { return first(); }
    auto size() { return n; }
    auto size() const { return n + 1; }
    auto first() const { return n - 1; }
};

int main()
{
    const View<int> v{4};
    if (v.end() != 6)
        return 1;
    if (v.begin() != 3)
        return 2;
    View<long> w{7};
    if (w.end() != 9 || w.size() != 7)
        return 3;
    return 0;
}
