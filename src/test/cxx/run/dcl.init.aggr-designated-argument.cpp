// [dcl.init.aggr]/3.1: a designated list as an argument of a member function
// overloaded with scalar parameters, for a reference to the aggregate
struct O {
    float width = 3;
    float tracking = 1;
};

struct S {
    int shape(int a, const O &o, int *h) const
    {
        return a + int(o.width * 100 + o.tracking) + (h != nullptr);
    }
    int shape(int a, float w, int *h) const { return a + int(w) + (h != nullptr); }
    int go(float w) const { return this->shape(1, {.width = w}, nullptr); }
    int go2(float w) const { return shape(1, {.tracking = w}, nullptr); }
};

int main()
{
    S s;
    const O &o = {.tracking = 9};
    __builtin_printf("%d %d %d\n", s.go(2), s.go2(5), int(o.width + o.tracking));
    return 0;
}
