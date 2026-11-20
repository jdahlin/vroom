#include <unordered_map>
int main() { std::unordered_map<int, int> m; m[1] = 2; return m.count(1) == 1 ? 0 : 1; }
