/* [temp.deduct.partial]/3.1: only the parameters the call has arguments for
   are ordered; the defaulted ones after them take no part (libc++'s
   regex_search on two const char *). */
extern "C" int printf(const char *, ...);

template <class C> struct regex {};

template <class B, class C> int search(B, B, const regex<C> &, int = 0)
{
    return 1;
}
template <class C>
int search(const C *, const C *, const regex<C> &, int = 0)
{
    return 2;
}

template <class T> int one(T, T = T()) { return 3; }
template <class T> int one(T *, int = 0, int = 0) { return 4; }

int main()
{
    const char *p = 0;
    int i = 0;
    regex<char> r;
    printf("%d %d %d %d\n", search(p, p, r), search(i, i, r), one(p), one(i));
    return 0;
}
