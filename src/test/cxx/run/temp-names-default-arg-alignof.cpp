/* [temp.names]: template-id-free default argument alignof(T) in a function template (libc++ __new/allocate.h:35) */
typedef decltype(sizeof(0)) size_t;
template <class T>
int f(size_t n, size_t align = alignof(T)) { return (int)(n + align); }
int main() { return f<int>(0) - (int)alignof(int); }
