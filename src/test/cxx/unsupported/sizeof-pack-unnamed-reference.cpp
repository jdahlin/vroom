/* sizeof...(T) in the body of a function template whose pack is an unnamed
   function parameter pack of reference types ([dcl.fct]/21 with an
   abstract-pack-declarator after &): the instantiation binds T as one
   type, not a pack. */
template <class... T> int count(T &...)
{
    return sizeof...(T);
}
int main()
{
    int a = 1;
    long b = 2;
    return count(a, b) != 2;
}
