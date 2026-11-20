/* [temp.names]: a literal-operator-id template (string literal operator template) */
template <char... Cs> constexpr int operator""_n() { return sizeof...(Cs); }
int main() { return 123_n - 3; }
