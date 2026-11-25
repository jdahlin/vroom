/* [temp.inst]/4: an operator on a class template specialization's prvalue
   whose destructor's definition is instantiated for it */
int destroyed;
template <class T> struct A {
    T v = 7;
    int operator==(int x) const
    {
        return v == x;
    }
    ~A()
    {
        destroyed += v;
    }
};
int main()
{
    int same = A<int>() == 7;
    return !(same == 1 && destroyed == 7);
}
