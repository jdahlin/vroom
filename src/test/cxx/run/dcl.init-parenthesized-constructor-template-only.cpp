/* [dcl.init]/17.6.2 with [over.match.ctor]: several parenthesized values call
   a constructor of a class whose only declared constructors are templates
   (std::__temp_value) */
extern "C" int printf(const char *, ...);

struct temp {
    int sum;
    template <class A, class... R> temp(A &a, R &&...r) : sum(a)
    {
        int all[] = {0, (sum = sum * 10 + r)...};
        (void)all;
    }
};

int main()
{
    int a = 1;
    temp x(a, 2);
    temp y(a, 3, 4);
    temp *p = new temp(a, 5);
    printf("%d %d %d %d\n", x.sum, y.sum, p->sum, temp(a, 6).sum);
    delete p;
}
