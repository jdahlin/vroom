/* [class.base.init]/2: a mem-initializer-id naming a member of an anonymous
   struct (clang extension) or union */
struct E {
    struct {
        int a;
        int b;
    };
    E() : a(40), b(2) {}
    int get() { return a + b; }
};
int main()
{
    E e;
    return e.get();
}
