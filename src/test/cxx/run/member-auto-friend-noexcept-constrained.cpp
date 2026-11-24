// [temp.inst]/4: a member function of a class template's specialization with a
// deduced return type is instantiated where its type is needed (a member
// alias naming decltype of its call); [class.mem]/6: a friend function's
// noexcept-specifier naming a parameter of the class being defined;
// [over.match.best]: a member with a trailing requires-clause against one
// without, same parameter types.
struct A {
    int v;
};
struct B {
    int v;
};

template <class T>
struct It {
private:
    static constexpr auto kind()
    {
        if constexpr (sizeof(T) > 1)
            return A{1};
        else
            return B{2};
    }

public:
    using kind_t = decltype(kind());
    T cur;
    friend T peek(const It &i) noexcept(noexcept(i.cur))
    {
        return i.cur;
    }
    auto step(int) requires(sizeof(T) > 1)
    {
        return 10;
    }
    void step(int)
    {
        ++cur;
    }
};

int main()
{
    It<int> a{3};
    It<char> b{4};
    It<int>::kind_t k{7};
    It<char>::kind_t j{8};
    if (peek(a) != 3)
        return 1;
    if (a.step(0) != 10)
        return 2;
    b.step(0);
    if (b.cur != 5)
        return 3;
    if (k.v != 7 || j.v != 8)
        return 4;
    return 0;
}
