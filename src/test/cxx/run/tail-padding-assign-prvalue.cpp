/* [abi:pod]: a trivial assignment of a class whose tail padding a derived
   class reuses copies only its data size; from a prvalue, the prvalue is
   materialized first ([basic.lval]/7) and its data size copied, so the
   derived class's member in the tail padding is kept. */
struct A {
    int x;
    char c;
    A() = default;
    A(int x, char c) : x(x), c(c) {}

  protected:
    int hidden = 0;
};
struct B : A {
    char tail;
};
A make(int v) { return A(v, 'a'); }
int main()
{
    B b;
    b.tail = 7;
    static_cast<A &>(b) = make(40);
    if (b.x != 40 || b.c != 'a' || b.tail != 7)
        return 1;
    static_cast<A &>(b) = A(2, 'b');
    return b.x + b.tail + (b.c == 'b') - 10;
}
