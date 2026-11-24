/* [dcl.fct.def.delete]/2, [over.match.oper]/3, [temp.local]/6, [class.mem]/6:
   a class template specialization that is a base class has its deleted
   copy assignment operator seen by __is_assignable, a const left operand
   finds no viable operator=, and a noexcept-specifier naming the pack of a
   partial specialization is analysed with the class complete. */
template <class T> struct is_move_assignable {
    static const bool value = __is_assignable(T &, T &&);
};
template <class T> struct is_const_move_assignable {
    static const bool value = __is_assignable(const T &, const T &&);
};

template <int I, class H> struct leaf {
    H value;
    leaf(H h) : value(h) {}
    leaf &operator=(const leaf &) = delete;
};

struct derived : leaf<0, int> {
    derived() : leaf<0, int>(0) {}
};

static_assert(!is_move_assignable<leaf<0, int>>::value, "deleted");
static_assert(!is_move_assignable<derived>::value, "deleted in the base");
static_assert(!is_const_move_assignable<leaf<0, int>>::value, "const object");

template <class... T> struct all_true { static const bool value = true; };

template <class I, class... T> struct impl;
template <class... T> struct impl<int, T...> {
    impl() noexcept(all_true<T...>::value) {}
    int n;
};

int main()
{
    impl<int, int, char> x;
    x.n = 0;
    return x.n;
}
