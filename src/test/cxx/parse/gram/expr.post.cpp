// [expr.post]: subscripts, calls, functional casts, member access,
// increments, the named casts, typeid.
namespace std { class type_info; }
struct S {
  int m;
  int a[3];
  constexpr int f(int x) const { return m + x; }
  constexpr const S *self() const { return this; }
};
struct B {
  virtual ~B();
};
struct D : B {};
constexpr S s{1, {2, 3, 4}};
constexpr int arr[2][2] = {{1, 2}, {3, 4}};
constexpr int twice(int x) { return 2 * x; }
typedef unsigned U;
enum class E { e = 3 };

static_assert(arr[1][0] + arr[0][1] == 5);
static_assert(s.a[2] - s.m == 3);
static_assert(twice(2) + twice(twice(1)) == 8);
static_assert(s.f(1) * 2 == 4);
static_assert(s.self()->f(2) == 3);
static_assert((&s)->m == 1);
static_assert(s.self()->self()->a[0] == 2);
static_assert(int(2.5) + U(1) == 3);
static_assert(int{2} + U{1} == 3);
static_assert(int() == 0);
static_assert(S{4, {}}.m + S{4, {5}}.a[0] == 9);
static_assert(char(65) == 'A');
static_assert(bool(2) + long(3) == 4);
static_assert(static_cast<int>(2.5) * 2 == 4);
static_assert(static_cast<U>(-1) > 0);
static_assert(static_cast<int>(E::e) + 1 == 4);
static_assert(*const_cast<const int *>(&s.m) == 1);
static_assert(static_cast<const S &>(s).m == 1);
static_assert(static_cast<const S *>(&s)->a[1] == 3);
static_assert(1 + static_cast<int>(1) + 1 == 3);

constexpr int steps(int x) {
  int y = x++;
  int z[2] = {++x, x--};
  z[0]++, z[1]--;
  --z[1];
  return z[0]-- - z[1]++ + y + x;
}
static_assert(steps(1) == 6);

const std::type_info &t1 = typeid(int);
const std::type_info &t2 = typeid(s);
const std::type_info &t3 = typeid(s.m + 1);
const std::type_info &t4 = typeid(const S *);
D d;
B *base = &d;
D *derived = dynamic_cast<D *>(base);
D &reference = dynamic_cast<D &>(*base);
long bits = reinterpret_cast<long>(base) + 1;
void destroy(S *q) { q->~S(); q->S::~S(); (*q).~S(); }
