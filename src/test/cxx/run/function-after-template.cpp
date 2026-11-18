template <class T> T twice(T x) { return x + x; }
long twice(long x) { return x * 3; }
long twice(long x);
int main() { return (twice(2L) == 6 && twice(2) == 4) ? 0 : 1; }
