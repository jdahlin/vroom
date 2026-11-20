#include <tuple>
int main() { auto t = std::make_tuple(1, 2.0, 'c'); return std::get<0>(t) == 1 && std::tuple_size<decltype(t)>::value == 3 ? 0 : 1; }
