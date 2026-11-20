#include <functional>
int main() { std::function<int(int)> f = [](int x) { return x + 1; }; return f(1) == 2 ? 0 : 1; }
