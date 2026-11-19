/* [temp.func.order]/6.2.2: of two function templates whose signatures
   tie, a constrained one is more specialized than one without constraints
   (both members too, the object argument aside) */
template <class T> concept Small = sizeof(T) <= 4;
template <class T> requires Small<T> int g(T) { return 1; }
template <class T> int g(T) { return 2; }
struct S {
    template <class T> requires Small<T> int f(T &&) const { return 3; }
    template <class T> int f(T &&) const { return 4; }
};
int main()
{
    S s;
    return g(1) + g(1L) * 10 + s.f(1) * 100 + s.f(1L) * 1000 - 4321;
}
