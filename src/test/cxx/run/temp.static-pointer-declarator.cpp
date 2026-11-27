/* [temp.static]/1: a static data member of a class template defined
   behind ptr-operators. */
extern "C" int printf(const char *, ...);
int five = 5, six = 6;
template <typename E> struct X {
    static E *g;
    static const E *const h;
    static E &r;
    static E **pp;
};
template <typename E> E *X<E>::g = nullptr;
template <typename E> const E *const X<E>::h = &five;
template <typename E> E &X<E>::r = six;
template <typename E> E **X<E>::pp = &X<E>::g;
template <typename T> struct R {
    static R *head;
    R *next;
    int v;
    R(int x) : next(head), v(x) { head = this; }
};
template <typename T> R<T> *R<T>::head;
template class X<int>;
int main()
{
    R<int> a(1), b(2);
    for (R<int> *p = R<int>::head; p; p = p->next)
        printf("%d ", p->v);
    X<int>::g = &five;
    X<int>::r++;
    printf("%d %d %d %d\n", **X<int>::pp, *X<int>::h, X<int>::r, six);
}
