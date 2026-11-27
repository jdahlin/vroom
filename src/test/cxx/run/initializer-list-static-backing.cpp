/* [dcl.init.list]/6, [class.temporary]/6: initializer_list backing arrays
   and local static references' temporaries of static storage */
namespace std {
template <class E> class initializer_list {
    const E *first;
    decltype(sizeof 0) count;
  public:
    constexpr initializer_list() : first(nullptr), count(0) {}
    constexpr const E *begin() const { return first; }
    constexpr const E *end() const { return first + count; }
};
}
extern "C" int printf(const char *, ...);
static int twice(int x) { return 2 * x; }
std::initializer_list<int> g = {twice(1), twice(2)};
static void show(std::initializer_list<int> l) { for (int e : l) printf("%d ", e); printf("\n"); }
static void clobber() { volatile int a[64]; for (int i = 0; i < 64; i++) a[i] = -1; }
static std::initializer_list<int> &st(int v) { static std::initializer_list<int> s = {twice(v), twice(v + 1)}; return s; }
int main() { st(5); clobber(); show(st(0)); show(g); }
static const int &sr(int v) { static const int &r = twice(v); return r; }

int other() { sr(4); clobber(); printf("%d\n", sr(9)); std::initializer_list<int> loc = {twice(3)}; clobber(); show(loc); return 0; }
struct run { run() { other(); } } r;
