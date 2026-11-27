// Explicit deduction guide whose pack parameter defaults to empty during
// deduction, making its return type a class template-id with too few template
// arguments (SFINAE); CTAD must fall back to the implicit constructor guide.
template <class Fn>
struct pipeable {
    explicit pipeable(Fn &&f) : fn(f) {}
    Fn fn;
};

template <class... Tag>
pipeable(typename Tag::__allow_ctad...) -> pipeable<Tag...>;

int main()
{
    auto p = pipeable(42);
    if (p.fn != 42)
        return 1;
    int x = 7;
    auto q = pipeable(x + 3);
    if (q.fn != 10)
        return 2;
    return 0;
}
