#include <variant>
int main() { std::variant<int, double> v = 1.5; return std::holds_alternative<double>(v) ? 0 : 1; }
