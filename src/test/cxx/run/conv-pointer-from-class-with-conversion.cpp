/* [over.match.conv]/1: a class whose conversion functions yield nothing that
   converts to a pointer is not assignable to one: substitution fails
   (std::is_assignable<const char *&, std::string const>) */
extern "C" int printf(const char *, ...);
struct view {};
struct str {
    operator view() const { return view(); }
};
struct holds {
    operator int *() const { return nullptr; }
};
template <class T, class U, class = decltype(*(T *)0 = *(U *)0)>
char assignable(int);
template <class T, class U> long assignable(...);
int main()
{
    printf("%d %d %d\n",
           (int)sizeof(assignable<const char *, const str>(0)),
           (int)sizeof(assignable<int *, str>(0)),
           (int)sizeof(assignable<int *, holds>(0)));
    return 0;
}
