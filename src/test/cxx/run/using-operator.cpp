// using ns::operator""_x; and using ns::operator+; bring those functions in (they were parsed and dropped:
// json's "..."_json literals in every test that writes `using nlohmann::literals::operator""_json`)
typedef unsigned long size_t;
namespace lit { inline int operator""_n(const char *s, size_t n) { return (int)n * 10 + (s[0] - '0'); } }
namespace ops { struct V { int v; }; inline int operator+(V a, int b) { return a.v + b; } }
using lit::operator""_n;
using ops::operator+;
int main() { ops::V v{3}; return "7ab"_n == 37 && v + 4 == 7 ? 0 : 1; }
