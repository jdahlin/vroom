/* libc++'s _LIBCPP_BEGIN_NAMESPACE_STD and _LIBCPP_USING_IF_EXISTS:
   type_visibility("default") on a namespace-definition changes nothing,
   and clang's using_if_exists ignores a using-declarator whose name is not
   found */
int va_like = 7;
namespace __attribute__((__type_visibility__("default"))) lib {
inline namespace __1 {
using ::va_like __attribute__((__using_if_exists__));
using ::not_declared_anywhere __attribute__((__using_if_exists__));
int twice() { return va_like * 2; }
} // namespace __1
} // namespace lib
namespace lib __attribute__((visibility("default"))) {
int three() { return 3; }
}
int main() { return lib::twice() + lib::va_like + lib::three() - 24; }
