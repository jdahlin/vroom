/* [temp.names]/2: after a qualifier whose primary template does not declare
   the name (a partial specialization does), a < followed by a
   template-argument-list and a call is a template-id */
enum Id { one = 1, two = 2 };
template <Id> struct traits {
    using type = int;
};
template <Id C, class T, bool B> struct helper;
template <Id C> struct helper<C, int, true> {
    template <class R> static R get(int a) { return static_cast<R>(a + C); }
};
template <Id C>
using command = helper<C, typename traits<C>::type, true>;
typedef long out;
struct S {
    static int run(bool b);
};
int S::run(bool b)
{
    int n = 0;
    if (b)
        n = static_cast<int>(command<two>::get<out>(3));
    return n;
}
int main()
{
    return S::run(true) != 5;
}
