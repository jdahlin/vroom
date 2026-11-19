/* [ext:gnu_library_builtins] clang's __builtin_addressof, libc++'s
   std::addressof ([specialized.addressof]/1): the object's address even
   when the class overloads operator& */
struct Hidden {
    int v;
    Hidden *operator&() { return nullptr; }
};
template <class T> constexpr T *address(T &x) { return __builtin_addressof(x); }
constexpr int through(int a)
{
    int *p = __builtin_addressof(a);
    *p += 1;
    return *p;
}
static_assert(through(6) == 7);
int main()
{
    Hidden h{5};
    if (&h != nullptr)
        return 1;
    Hidden *p = __builtin_addressof(h);
    if (!p || p->v != 5)
        return 2;
    const Hidden c{h.v + 1};
    const Hidden *q = __builtin_addressof(c);
    if (q->v != 6 || address(c)->v != 6 || address(h) != p)
        return 3;
    int i = 4;
    return *address(i) - 4;
}
