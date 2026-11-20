#include <string>
int main() { std::string s = "abc"; s += "def"; return s.size() == 6 && s[2] == 'c' ? 0 : 1; }
