// [over.ics.list]/10: a braced-init-list argument of a scalar parameter of a
// constructor, an enumeration's included.
enum class W { kNo, kYes };
struct B {
};
struct P {
    P(B, int n, W w = W::kYes, const int &k = 3) : v(n * 100 + int(w) * 10 + k)
    {
    }
    int v;
};
int main()
{
    P a(B(), 1, {});
    P b(B(), {}, W::kYes, {});
    P *c = new P(B(), {2}, {}, {4});
    int r = a.v + b.v + c->v - (103 + 10 + 204);
    delete c;
    return r;
}
