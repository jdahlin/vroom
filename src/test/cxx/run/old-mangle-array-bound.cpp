template<class T, int N> int length(T (&)[N]) { return N; }
int main() { int a[7]; return length(a) != 7; }
