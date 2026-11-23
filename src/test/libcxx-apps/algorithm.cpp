#include <algorithm>
int main() { int a[] = {3, 1, 2}; std::sort(a, a + 3); return a[0] == 1 && *std::max_element(a, a + 3) == 3 ? 0 : 1; }
