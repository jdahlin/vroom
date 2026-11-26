/* [temp.inst]/2: a specialization is implicitly instantiated where a
   completely-defined type is required.  Alloc<Iter<Y>> is first named
   while Y is being defined (Iter<Y> waits for Y), where only a pointer to
   it is declared; __is_nothrow_constructible then requires it complete,
   still inside Y (libc++'s allocator of a container's element). */
template <class T> struct Alloc {
    int n = 7;
    Alloc() noexcept {}
};
template <class T> struct Iter {
    T *p;
};
struct Y {
    Iter<Y> *i;
    Alloc<Iter<Y>> *a;
    static constexpr bool b = __is_nothrow_constructible(Alloc<Iter<Y>>);
};
int main()
{
    Alloc<Iter<Y>> a;
    return !Y::b || a.n != 7;
}
