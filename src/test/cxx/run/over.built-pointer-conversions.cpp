/* [over.built], [over.match.best]: a member operator[] against the
   built-in subscript through conversion functions to pointers, and the
   built-in operators of a class converting to T * and const T *. */
extern "C" int printf(const char *, ...);
template <class T> struct Auto {
    T data[8] = {};
    operator T *() { which = 'm'; return data; }
    operator const T *() const { which = 'c'; return data; }
    T &operator[](int i) { which = 'M'; return data[i]; }
    const T &operator[](int i) const { which = 'C'; return data[i]; }
    mutable char which = '-';
};
template <class T> struct Ptr {
    T data[8] = {};
    operator T *() { which = 'm'; return data; }
    operator const T *() const { which = 'c'; return data; }
    mutable char which = '-';
};
struct S { int v; };
int main()
{
    Auto<S> a;
    const Auto<S> &ca = a;
    unsigned long i = 2;
    int j = 3;
    a[i].v = 5;
    printf("%c %d\n", a.which, a.data[2].v);
    printf("%d %c\n", ca[i].v, a.which);
    a[j].v = 7;
    printf("%d %c\n", ca[j].v, a.which);
    S *p = a + i;
    printf("%d %c\n", p->v, a.which);
    const S *q = ca + j;
    printf("%d %c\n", q->v, a.which);
    printf("%d %c\n", (a + 1 + i)->v, a.which);
    Ptr<unsigned char> u;
    const Ptr<unsigned char> &cu = u;
    u[i] = 9;
    printf("%d %c\n", u.data[2], u.which);
    printf("%d %c\n", cu[i], u.which);
    unsigned char *w = u + i * 1 + 0;
    printf("%d %c %d\n", *w, u.which, static_cast<int>((u + j) - u.data));
}
