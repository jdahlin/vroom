/* [temp.names]: a concept-id as a type-constraint and in a requires-clause */
template <class T> concept Small = sizeof(T) <= 4;
template <class T, class U> concept Same = __is_same(T, U);
template <Small T> int f(T) { return 1; }
template <class T> requires Same<T, long> int f(T) { return 2; }
template <class T> requires (Small<T> && !Same<T, int>) int f(T) { return 3; }
int main() { return f(1) + f(2L) + f('c') - 6; }
