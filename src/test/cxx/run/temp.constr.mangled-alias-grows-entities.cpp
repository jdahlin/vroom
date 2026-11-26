/* [temp.constr.decl]: mangling a requires-clause that names an alias template
   makes entities.  The lowering held a reference to the symbol's entity across
   it: a use after free once the entity table grew (seen under ASan; several
   functions, so that one of the manglings grows it). */
template <class T> using same_type = T;
template <class A, class B> concept same = __is_same(A, B);

template <class T> requires same<same_type<T>, T> int f0(T) { return 1; }
template <class T> requires same<same_type<T>, T> int f1(T) { return 1; }
template <class T> requires same<same_type<T>, T> int f2(T) { return 1; }
template <class T> requires same<same_type<T>, T> int f3(T) { return 1; }
template <class T> requires same<same_type<T>, T> int f4(T) { return 1; }
template <class T> requires same<same_type<T>, T> int f5(T) { return 1; }

int main()
{
    int n = f0(0) + f1(0) + f2(0) + f3(0) + f4(0) + f5(0);
    return n != 6;
}
