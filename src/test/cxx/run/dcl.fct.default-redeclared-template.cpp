/* [dcl.fct.default]/4: a function template's default arguments, given by
   its first declaration, are its specializations' though a later one
   defines it */
template <class T> int level(T *v, bool takeoff = false);
template <class T> int level(T *v, bool takeoff) { return *v + takeoff; }
int main()
{
    int x = 4;
    return !(level(&x) == 4 && level(&x, true) == 5);
}
