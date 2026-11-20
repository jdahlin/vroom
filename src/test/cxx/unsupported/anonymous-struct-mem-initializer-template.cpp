/* cxx-unsupported: lowering */
/* [class.base.init]/2: the same in a class template's constructor, used: the
   instantiated body is not analysed, so no code is made for it (it must not
   be an empty constructor) */
template <class T> struct E {
    struct {
        T a;
        int b;
    };
    E() : a(40), b(2) {}
    int get() { return a + b; }
};
int main()
{
    E<int> e;
    return e.get();
}
