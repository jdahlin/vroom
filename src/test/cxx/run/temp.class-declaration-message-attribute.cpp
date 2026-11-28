// [dcl.attr.deprecated], [temp.class]/1: libc++ declares common_reference
// with a message-taking attribute and no definition.  Refusing that made
// later explicit instantiations of basic_string fail, whose recovery hid
// namespace std, and V8's base::BitField (a deduced return type used in its
// class) was then reported ill-formed ([dcl.spec.auto]).
template <class... T>
struct [[clang::no_specializations("no")]] common_reference;
template <class... T> using common_reference_t =
    typename common_reference<T...>::type;
template <class T> struct [[deprecated("old")]] Old;
template <class T> struct Old { T v; };

namespace std {
inline namespace v1 {
using size = unsigned long;
}
}

// the shape of V8's base::BitField
namespace base {
template <class T> std::size length(T) { return sizeof(T); }
template <class... T> struct common_reference {};
template <class T> struct Field {
    static constexpr auto to_value(T v) { return v; }
    using value = decltype(to_value(T()));
};
}

int main() {
    base::Field<int>::value v = 3;
    std::size n = base::length(v);
    return n != 4;
}
