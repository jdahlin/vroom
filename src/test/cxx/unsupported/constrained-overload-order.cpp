/* [temp.func.order]/6, [temp.constr.order]: of two function templates
   whose signatures tie, the more constrained one is chosen; the
   constraints' normal forms are not compared yet, so the call is refused */
template <class T> concept Small = sizeof(T) <= 4;
template <class T> requires Small<T> int g(T) { return 1; }
template <class T> int g(T) { return 2; }
int a = g(1);
