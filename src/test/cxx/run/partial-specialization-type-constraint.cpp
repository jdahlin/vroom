/* [temp.param]/4: a type-constraint on a class template partial
   specialization's parameter, checked when it is matched
   ([temp.class.spec.match]/2); two differing only in it are distinct */
template <class T> concept big = sizeof(T) > 4;
template <class T> concept small = sizeof(T) == 1;
template <class T> struct S { static constexpr int v = 0; };
template <big T> struct S<T> { static constexpr int v = 1; };
template <class T> struct P { static constexpr int v = 0; };
template <big T> struct P<T *> { static constexpr int v = 2; };
template <small T> struct P<T *> { static constexpr int v = 3; };
int main()
{
    return S<char>::v + S<long>::v * 10 + P<long *>::v * 100 +
           P<char *>::v * 1000 + P<short *>::v * 10000 - 3211;
}
