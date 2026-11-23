#include <map>
int main() { std::map<int, int> m; m[1] = 2; m[3] = 4; return m.find(3)->second == 4 ? 0 : 1; }
