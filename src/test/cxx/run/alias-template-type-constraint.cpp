/* [temp.param]/4: a type-constraint on an alias template's parameter,
   satisfied by the arguments of a template-id naming it ([temp.names]/7) */
template <class T> concept big = sizeof(T) > 4;
template <big T> using A = T;
template <class T>
    requires(sizeof(T) == 1)
using B = T *;
int main()
{
    A<long> x = 3;
    char c = 4;
    B<char> p = &c;
    return x + *p - 7;
}
