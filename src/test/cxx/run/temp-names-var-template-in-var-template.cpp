/* [temp.names]: a variable template named in another variable template initializer (is_swappable.h:39) */
template <class T, class U>
inline const bool base_v = false;
template <class T>
inline const bool derived_v = base_v<T&, T&>;
int main() { return derived_v<int> ? 1 : 0; }
