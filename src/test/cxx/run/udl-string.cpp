// "..."_x calls operator""_x(const char *, size_t) (json's "..."_json); both spellings of the declaration,
// found through an inline namespace and a using-directive; mangled li<len><suffix>
typedef unsigned long size_t;
namespace lit { inline namespace l2 {
struct S { const char *p; size_t n; };
inline S operator ""_s(const char *p, size_t n) { return S{p, n}; }
inline int operator"" _k(const char *p, size_t n) { return (int)n + p[0]; }
} }
using namespace lit;
int main() { S a = "abc"_s; S b = R"(x"y)"_s; return (a.n == 3 && a.p[2] == 'c' && b.n == 3 && b.p[1] == '"' && "a" "b"_k == 'a' + 2) ? 0 : 1; }
