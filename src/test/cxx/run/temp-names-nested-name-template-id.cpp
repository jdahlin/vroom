/* [temp.names]: a template-id as a nested-name-specifier */
template <class T> struct A { struct In { static int v() { return sizeof(T); } }; static int w() { return 2; } };
int main() { return A<int>::In::v() - 4 + A<char>::w() - 2; }
