// [dcl.typedef]/2: a typedef-name "is ... a synonym for another type";
// through one that names a class, [basic.lookup.qual]/1 finds the class's
// members, also when the typedef-name was declared before the class was
// defined ([class.pre]/2: the class-specifier defines the class the
// earlier declaration declared).  (Found in nlohmann/json: using json =
// basic_json<>; before basic_json's definition, then json::json_pointer.)
namespace n {
template <class T> struct ptr { int i; };
template <class T = int> struct basic;
using J = basic<>;
template <class T> struct basic { using pointer = ::n::ptr<basic>; };

struct S;
typedef S T;
struct S { typedef int m; static constexpr int k = 7; };

struct Defined { using size = unsigned; };
typedef Defined D;
using A = Defined;
}
n::J::pointer p{1};
n::T::m y = 2;
static_assert(n::T::k == 7);
n::D::size z = 3;
n::A::size w = 4;
using size_t = decltype(sizeof 0);
n::J::pointer operator""_ptr(const char *, size_t);
