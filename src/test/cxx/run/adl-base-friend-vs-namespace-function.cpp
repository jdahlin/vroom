// ADL for a class: its base's hidden friend and its namespace's function are one candidate set (json's
// NLOHMANN_DEFINE_DERIVED_TYPE_NON_INTRUSIVE: to_json(j, D) for D, not the base's friend to_json(j, const B&))
extern "C" int printf(const char*, ...);
struct J { int v; };
namespace ns {
struct B { int a; template<class T> friend void to_json(T& j, const B& b) { j.v += 1; } };
struct D : B { int c; };
template<class T> void to_json(T& j, const D& d) { to_json(j, static_cast<const B&>(d)); j.v += 10; }
}
template<class T> void call(J& j, const T& t) { to_json(j, t); }
int main() { J j{0}; ns::D d{}; call(j, d); printf("%d\n", j.v); }
