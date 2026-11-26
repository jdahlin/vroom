/* [dcl.init.ref]/5.4: a class whose conversion functions yield nothing that
   converts to int is no argument for const int & (std::is_assignable of
   valarray<int> & from std::string const) */
extern "C" int printf(const char *, ...);
struct view {};
struct str {
    operator view() const { return view(); }
};
struct pointer_like {
    template <class T> operator T *() const { return nullptr; }
};
struct target {
    int operator=(const int &v) { return v; }
};
template <class T, class U, class = decltype(*(T *)0 = *(U *)0)>
char assignable(int);
template <class T, class U> long assignable(...);
int main()
{
    printf("%d %d\n", (int)sizeof(assignable<target, const str>(0)),
           (int)sizeof(assignable<target, pointer_like>(0)));
    return 0;
}
