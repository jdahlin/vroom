/* [dcl.type.decltype]/1: an unparenthesized id-expression gives the
   entity's declared type, otherwise an lvalue gives T&, an xvalue T&& and
   a prvalue T; the operand is unevaluated.  libc++'s stddef.h:
   typedef decltype(nullptr) nullptr_t */
typedef decltype(nullptr) nullptr_t;
static int calls;
static int side() { return ++calls; }
int main()
{
    int x = 1;
    int &rx = x;
    decltype(x) copy = x;          /* int */
    decltype((x)) alias = x;       /* int& */
    decltype(rx) alias2 = x;       /* int& (the declared type) */
    decltype(x + 1) sum = 40;      /* int */
    decltype(side()) unevaluated = 2; /* int, side() not called */
    decltype(static_cast<int &&>(x)) moved = static_cast<int &&>(x); /* int&& */
    nullptr_t none = nullptr;
    copy = 100;
    alias = 3;   /* x = 3 */
    alias2 += 4; /* x = 7 */
    moved += 1;  /* x = 8 */
    return (x != 8) + (copy != 100) * 2 + (sum != 40) * 4 + calls * 8 +
           (none != nullptr) * 16 + (unevaluated != 2) * 32 +
           (sizeof(decltype((copy))) != sizeof(int)) * 64;
}
