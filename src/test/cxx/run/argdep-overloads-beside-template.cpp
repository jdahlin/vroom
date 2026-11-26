/* [basic.lookup.argdep]/4.2: a call whose unqualified lookup finds a
   function template takes every overload of the functions argument-dependent
   lookup finds, not only the first */
struct Data {};
struct FooBar {};
int f(Data &) { return 1; }
int f(FooBar &) { return 2; }
namespace N {
template <class T> int f(T *) { return 3; }
template <class T> auto call(T &v) -> decltype(f(v)) { return f(v); }
}
int main()
{
    FooBar b;
    return N::call(b) - 2;
}
