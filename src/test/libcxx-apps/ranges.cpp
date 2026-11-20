#include <ranges>
#include <vector>
int main() { std::vector<int> v{1, 2, 3, 4}; int s = 0; for (int x : v | std::views::filter([](int i) { return i % 2 == 0; })) s += x; return s == 6 ? 0 : 1; }
