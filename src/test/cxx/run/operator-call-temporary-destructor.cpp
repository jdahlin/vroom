/* [class.temporary]/4: a temporary with a destructor, the object of an
   overloaded operator's member call, is destroyed at the end of the
   full-expression. */
int destroyed;
int order;
struct X {
    ~X() { destroyed = destroyed * 10 + 1; }
    int operator()(int a) const { return a + destroyed; }
    int operator[](int a) const { return a * 2; }
    int operator+(int a) const { return a; }
};
int use(int a, int b) { return a + b; }
int main()
{
    int r = X()(2);
    if (r != 2 || destroyed != 1)
        return 1;
    int s = use(X()[3], X() + 4);
    if (s != 10 || destroyed != 1111)
        return 2;
    return 0;
}
