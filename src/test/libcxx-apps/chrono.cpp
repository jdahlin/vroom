#include <chrono>
int main() { auto d = std::chrono::milliseconds(1500); return std::chrono::duration_cast<std::chrono::seconds>(d).count() == 1 ? 0 : 1; }
