#include <span>
int main() { int a[] = {1, 2, 3}; std::span<int> s(a); return s.size() == 3 && s[1] == 2 ? 0 : 1; }
