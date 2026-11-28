/* a template-id's template parameter argument in a requirement of a member
   template's requires-clause (libc++'s requires { __fun(std::declval<_Tp>());
   } in ref_view's constructor): the leveled parameter */
namespace n {
template <class T> T &&declval() noexcept;
template <class R> struct view {
    R *p;
    static void fun(R &);
    static void fun(R &&) = delete;
    template <class T>
        requires requires { fun(n::declval<T>()); }
    view(T &&t) : p(&t) {}
};
}

int main()
{
    int x = 3;
    n::view<int> v(x);
    return *v.p - 3;
}
