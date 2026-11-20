/* [temp.names]: a template-id naming a variable template in an expression */
template <class T> constexpr int size_v = sizeof(T);
template <class T, class U> constexpr bool same_v = false;
template <class T> constexpr bool same_v<T, T> = true;
int main() { return size_v<int> - 4 + (same_v<int, int> ? 0 : 1) + (same_v<int, long> ? 1 : 0); }
