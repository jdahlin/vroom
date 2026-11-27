/* [class.temporary]/6: temporaries bound to static references */
extern "C" int printf(const char *, ...);
static int twice(int x) { return 2 * x; }
const int &g = twice(21);
struct s { int a; s(int x) : a(x) {} };
const s &gs = s(twice(1));
inline const int &inl() { static const int &r = twice(2); return r; }
template <class T> const T &tm() { static const T &r = T(twice(3)); return r; }
static void clobber() { volatile int a[64]; for (int i = 0; i < 64; i++) a[i] = -1; }
int main() { inl(); tm<int>(); static const s &ls = s(twice(5)); clobber(); printf("%d %d %d %d %d\n", g, gs.a, inl(), tm<int>(), ls.a); }
