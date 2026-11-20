/* [class.friend]/2: a friend declaration in a partial or explicit
   specialization of a class template (libc++ unique_ptr<T[], D>) */
struct Peek;
template <class T> struct U { int p = 1; };
template <class T> struct U<T*> { friend struct Peek; private: int q = 4; };
template <> struct U<int> { friend struct Peek; private: int r = 5; };
struct Peek { static int f(U<char*> a, U<int> b) { return a.q + b.r; } };
int main() { return Peek::f(U<char*>(), U<int>()) - 9; }
