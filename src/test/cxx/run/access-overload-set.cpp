// [class.access]/4: of an overloaded static member function called by a
// qualified or unqualified name, access is that of the function selected,
// not of the first declaration of the set.
struct Rect {
    int left, right;

private:
    static bool Intersects(int al, int ar, int bl, int br)
    {
        return al < br && bl < ar;
    }

public:
    static bool Intersects(const Rect &a, const Rect &b)
    {
        return Intersects(a.left, a.right, b.left, b.right);
    }
};

struct Derived : Rect {
    static bool test(const Rect &a, const Rect &b)
    {
        return Intersects(a, b) && Rect::Intersects(a, b);
    }
};

template <class R> static bool through(const R &a, const R &b)
{
    return R::Intersects(a, b);
}

int main()
{
    Rect a{0, 4}, b{2, 6}, c{8, 9};
    if (!Rect::Intersects(a, b) || Rect::Intersects(a, c))
        return 1;
    if (!through(a, b) || through(a, c))
        return 2;
    if (!Derived::test(a, b))
        return 3;
    return 0;
}
