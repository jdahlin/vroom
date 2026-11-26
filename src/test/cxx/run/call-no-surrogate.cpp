/* [over.call.object]/2: a class whose conversion functions yield no
   function pointer or reference has no surrogate call function: a call no
   operator() takes is a substitution failure */
extern "C" int printf(const char *, ...);

struct number {
    operator int() const
    {
        return 1;
    }
    void operator()(int *) const {}
};

struct only_converts {
    operator long() const
    {
        return 2;
    }
};

template <class T> T &make();

template <class F, class = decltype(make<F>()(1))> int callable(int)
{
    return 1;
}
template <class F> int callable(long)
{
    return 0;
}

int main()
{
    printf("%d %d\n", callable<number>(0), callable<only_converts>(0));
    return 0;
}
