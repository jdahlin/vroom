/* [over.match.best]/2.5 within a user-defined conversion: of two converting
   constructor templates with alike conversions the more specialized one
   ([temp.func.order]) converts; there is no ambiguity */
extern "C" int printf(const char *, ...);

struct R {
    int n;
};

struct J {
    int how;
    template <class T> J(T &&) : how(1) {}
    template <class T> J(const T &) : how(2) {}
};

template <class T> T &make();
template <class T, class U, class = decltype(make<T>() = make<U>())>
constexpr bool assignable(int)
{
    return true;
}
template <class T, class U> constexpr bool assignable(long)
{
    return false;
}

static int how(J j)
{
    return j.how;
}

int main()
{
    const R r{3};
    J a = r;
    R m{4};
    J b = m;
    printf("%d %d %d %d\n", how(r), a.how, b.how, assignable<J, const R &>(0));
    return 0;
}
