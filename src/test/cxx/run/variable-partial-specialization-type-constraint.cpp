/* [temp.param]/4 on a variable template's partial specialization: the
   primary's arguments, more constrained (libc++'s <numbers>: template
   <floating_point _Tp> inline constexpr _Tp e_v<_Tp> = ...) */
template <class T> concept wide = sizeof(T) >= 8;
template <class T> constexpr int v = 1;
template <wide T> constexpr int v<T> = 2;
template <class T>
    requires(sizeof(T) == 1)
constexpr int v<T *> = 3;
int main() { return v<char> + v<long> * 10 + v<char *> * 100 - 321; }
