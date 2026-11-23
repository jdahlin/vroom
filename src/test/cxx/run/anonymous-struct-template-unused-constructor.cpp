/* [temp.inst]/4: a constructor of a class template with a mem-initializer of
   a member of an anonymous struct, never used: the class is still usable
   through the constructor that is */
template <class T> struct E {
    struct {
        T a;
        int b;
    };
    E() : a(40), b(2) {}
    E(T x) { a = x; b = 2; }
    int get() { return a + b; }
};
int main()
{
    E<int> e(40);
    return e.get() == 42 ? 0 : 1;
}
