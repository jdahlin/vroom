/* [dcl.fct.def.delete]/2: a requirement that calls a deleted function,
   a template's specialization or the only function of its name, is not
   satisfied (std::ranges::swap's poison pill) */
template <class T> void swap(T &, T &) = delete;
void h(int) = delete;
template <class T> concept can = requires(T &a, T &b) { swap(a, b); };
template <class T> concept ch = requires(T a) { h(a); };
struct Item {
    int a;
};
static_assert(!ch<int> && !can<int> && !can<Item>);
struct F {
    template <class T>
        requires can<T>
    int operator()(T &x) const
    {
        swap(x, x);
        return 1;
    }
    template <class T>
        requires(!can<T>)
    int operator()(T &) const
    {
        return 2;
    }
};
int main()
{
    Item x{1};
    return F{}(x) - 2;
}
