// [namespace.def]/1: an attribute-specifier-seq after namespace; one that
// changes nothing vcc generates ([dcl.attr.nodiscard], [dcl.attr.unused]:
// clang warns where it does not apply to a namespace).
namespace [[maybe_unused]] N1 {
int x = 1;
}
namespace N2 [[deprecated]] {
int y = 2;
}
inline namespace [[]] {
int z = 3;
}
int sum() { return N1::x + N2::y + z; }
