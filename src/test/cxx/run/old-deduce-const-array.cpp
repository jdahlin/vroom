// const T& deduced from a const char array: T is char[N] (an array's qualifiers are its elements'), as clang's
template <class T> int f(const T &) { return 0; }
int main() { const char s[3] = "ab"; char t[2] = "a"; const int ci = 1; return f("xy") + f(s) + f(t) + f(ci); }
