/* A lambda in a default member initializer that a substitution analyses: an
   aggregate's copy-list-initialization in a partial specialization's
   decltype (libc++'s __is_implicitly_default_constructible, std::pair). */
static int called;
struct I {
    void (*fp)(int) = [](int n) { called += n; };
};
template <class T> void test(T);
template <class U, class = void> struct implicit {
    static const bool value = false;
};
template <class U> struct implicit<U, decltype(test<U const &>({}))> {
    static const bool value = true;
};
template <class A, class B> struct pr {
    A first;
    B second;
    template <bool E = implicit<B>::value>
    explicit(!E) constexpr pr() : first(), second()
    {
    }
};

int main()
{
    static_assert(implicit<I>::value);
    pr<int, I> p;
    p.second.fp(2);
    I i{};
    i.fp(3);
    return called - 5;
}
