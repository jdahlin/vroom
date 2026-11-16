// an unnamed function parameter pack (A &&...) in a member template, its call in a decltype of a variable template's
// partial specialization (libc++'s __has_construct_v for an allocator's construct(T *, Args &&...))
template <class T> T &&dv();
template <class T> struct B { template <class... A> void construct(T *, A &&...) {} };
template <class, class Al, class... Ar> inline const bool has_c = false;
template <class Al, class... Ar> inline const bool has_c<decltype((void)dv<Al>().construct(dv<Ar>()...)), Al, Ar...> = true;
int a() { return has_c<void, B<int> &, int *, const char (&)[1]>; }
int b() { return has_c<void, B<int> &, int *, int>; }
int main() { return a() == 1 && b() == 1 ? 0 : 1; }
