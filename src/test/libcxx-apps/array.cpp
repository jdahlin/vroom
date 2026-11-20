#include <array>
int main() { std::array<int, 3> a{1, 2, 3}; return a.size() == 3 && a.at(1) == 2 ? 0 : 1; }
