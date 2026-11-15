// std: c++20
unsigned long long operator""_count(unsigned long long n) { return n; }
namespace units {
long double operator "" _very_long_literal_suffix_for_mangling(long double n) { return n; }
char operator""_count(char c) { return c; }
struct Task { };
int operator co_await(Task) { return 1; }
}
void operator_helper() {}
