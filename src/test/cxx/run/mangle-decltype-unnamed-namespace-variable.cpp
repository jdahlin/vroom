// [abi:expressions] ::ns::v(args) in a decltype of a template's declaration, v a variable of ns's
// unnamed namespace ([namespace.unnamed]/1: found as ns's member): L _Z <its encoding> E as the
// callee (json's adl_serializer::to_json: decltype(::nlohmann::to_json(j, std::forward<T>(val)), void()))
namespace std {
template <class T> T &&forward(T &t) noexcept { return static_cast<T &&>(t); }
} // namespace std
namespace nl {
struct to_json_fn {
  template <class J, class T> void operator()(J &j, T &&v) const { j = (int)v; }
};
namespace {
to_json_fn to_json;
}
template <class V, class = void> struct adl {
  template <class J, class T = V>
  static auto to_json(J &j, T &&val) noexcept(noexcept(::nl::to_json(j, std::forward<T>(val))))
      -> decltype(::nl::to_json(j, std::forward<T>(val)), void()) {
    ::nl::to_json(j, std::forward<T>(val));
  }
};
} // namespace nl
int main() {
  int j = 0;
  bool b = true;
  nl::adl<bool>::to_json(j, b);
  return j - 1;
}
