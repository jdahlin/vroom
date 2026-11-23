#include <vector>
int main() { std::vector<int> v{1, 2, 3}; v.push_back(4); int s = 0; for (int x : v) s += x; return s == 10 ? 0 : 1; }
