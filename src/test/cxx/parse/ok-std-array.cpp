// std::array: a value argument for the bound, the array<T, 0> partial specialization, an element
// whose member's type comes through a using-declaration (std::uint64_t).
#include <array>
#include <cstdint>
struct cp { std::uint64_t f; int e; };
std::array<cp, 79> a; std::array<int, 3> b; std::array<int, 0> z;
