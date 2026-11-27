/* [temp.over.link]: a member template's out-of-class definition whose
   non-type parameter's type is spelled with the class's template parameter
   where the declaration used a member typedef (libc++'s priority_queue) */
extern "C" int printf(const char *, ...);
template <class C, class A> struct uses {
    static const bool value = true;
};
template <bool, class T> struct enable {};
template <class T> struct enable<true, T> {
    typedef T type;
};
template <class C> struct queue {
    typedef C container;
    int v;
    template <class A, typename enable<uses<container, A>::value, int>::type = 0>
    queue(const A &a);
};
static queue<int> *keep;
template <class C>
template <class A, typename enable<uses<C, A>::value, int>::type>
queue<C>::queue(const A &a) : v((int)a)
{
}
int main()
{
    queue<int> q(41);
    printf("%d\n", q.v + (keep == nullptr));
}
