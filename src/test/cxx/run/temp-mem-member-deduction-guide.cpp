/* [temp.mem]/1, [over.match.class.deduct], [temp.deduct.guide]: class template argument deduction for a member class template, from its constructor (implicit guides) and from a deduction guide declared for it in the enclosing class */
struct O {
    template <class T> struct Box {
        T v;
        Box(T t) : v(t) {}
    };
    template <class T> struct Tagged {
        T v;
        template <class U> Tagged(U u) : v(T(u)) {}
    };
    template <class U> Tagged(U) -> Tagged<long>;
};
int main()
{
    O::Box b(3);
    O::Tagged t('a');
    return b.v - 3 + int(t.v) - 'a' + (sizeof(t.v) != 8);
}
