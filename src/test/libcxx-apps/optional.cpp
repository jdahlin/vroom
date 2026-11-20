#include <optional>
int main() { std::optional<int> o; o = 5; return o.value_or(0) == 5 ? 0 : 1; }
